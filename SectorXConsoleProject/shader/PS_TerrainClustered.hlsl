#include "_GlobalTypes.hlsli"

// t25: GPU 側とレイアウト完全一致
struct ClusterParam
{
    int splatSlice; // Texture2DArray のスライス番号
    float2 layerTiling[4]; // 各素材のタイル(U,V)
};

// ====== レジスタ割り当て（これまでの設計に準拠） ======
Texture2D gLayer0 : register(t20);
Texture2D gLayer1 : register(t21);
Texture2D gLayer2 : register(t22);
Texture2D gLayer3 : register(t23);
Texture2DArray gSplat : register(t24); // クラスタごとの重み (RGBA) を slice で参照
StructuredBuffer<ClusterParam> gClusters : register(t25); // 全クラスタのパラメータ表

SamplerState gPointClamp : register(s3);

// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z) ※同上
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)

    float2 gSplatInvSize; // 1/width, 1/height (splat texture用)

    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height
};

// VS 出力（worldPos を追加）
struct VSOut
{
    float4 pos : SV_Position;
    float3 worldPos : TEXCOORD1; // 少なくとも x,z を使用
    float viewDepth : TEXCOORD2;
    float3 nrm : NORMAL0; // 必要なら
};


// ユーティリティ
// rel: 「何セル目か」を表す連続座標 (0..gDimX, 0..gDimZ)
struct ClusterCoord
{
    uint id;
    float2 localUV; // 0..1
};

ClusterCoord ComputeCluster(float2 worldXZ)
{
    ClusterCoord r;

    float2 rel = (worldXZ - gOriginXZ) / gClusterXZ;

    // セル index
    int2 ij = int2(floor(rel + 1e-6)); // ここでだけ epsilon をかける
    ij = clamp(ij, int2(0, 0), int2(int(gDimX) - 1, int(gDimZ) - 1));
    r.id = uint(ij.y) * gDimX + uint(ij.x);

    // 同じ rel から localUV を求める
    float2 cellBase = float2(ij);
    r.localUV = rel - cellBase; // = frac(rel) と同じ意味

    return r;
}

float4 NormalizeWeights(float4 w)
{
    w = saturate(w);
    float s = max(1e-5, w.r + w.g + w.b + w.a);
    return w / s;
}

float4 SampleSplatBilinear_NoMip(Texture2DArray tex, SamplerState sampPoint, float2 uv01, int slice, float2 invSize)
{
    // uv(0..1) -> texel space
    float2 p = uv01 / invSize - 0.5f; // = uv*size - 0.5
    float2 i = floor(p);
    float2 f = p - i; // frac

    // 4隅の中心UVに戻す
    float2 uv00 = (i + float2(0.5f, 0.5f)) * invSize;
    float2 uv10 = uv00 + float2(invSize.x, 0);
    float2 uv01_ = uv00 + float2(0, invSize.y);
    float2 uv11 = uv00 + invSize;

    // 同一 slice だけをPointで4回
    float4 s00 = tex.SampleLevel(sampPoint, float3(uv00, slice), 0);
    float4 s10 = tex.SampleLevel(sampPoint, float3(uv10, slice), 0);
    float4 s01v = tex.SampleLevel(sampPoint, float3(uv01_, slice), 0);
    float4 s11 = tex.SampleLevel(sampPoint, float3(uv11, slice), 0);

    // bilinear
    float4 sx0 = lerp(s00, s10, f.x);
    float4 sx1 = lerp(s01v, s11, f.x);
    return lerp(sx0, sx1, f.y);
}

// === PS（ワンドロー本体） ===
PS_PRBOutput main(VSOut i)
{
    // ピクセルの worldPos.xz -> クラスタID
    ClusterCoord c = ComputeCluster(i.worldPos.xz);
    uint cid = c.id;
    float2 suv = c.localUV;

    ClusterParam p = gClusters[cid];

    // 2) スプラット重み：Texture2DArray なら slice 指定
    float4 w = float4(1, 0, 0, 0);

    //簡易的に距離でバイリニア補間にするかどうかを切り替え
    if (i.viewDepth < 50.0f)
    {
        w = SampleSplatBilinear_NoMip(gSplat, gPointClamp, suv, p.splatSlice, gSplatInvSize);
    }
    else
    {
        // 遠距離なら簡易版でOK
        w = gSplat.SampleLevel(gPointClamp, float3(suv, p.splatSlice), 0);
    }

    // 正規化
    w = saturate(w);
    w /= max(1e-5, dot(w, 1));


    // 3) 素材4：連続感が欲しければ world ベースでタイルするのがおすすめ
    //    例: ワールドXZをスケール（完全にクラスタ無関係の連続タイル）
    //float2 uvWorld = i.worldPos.xz; // 必要に応じて / overallScale
    float4 c0 = gLayer0.Sample(gSampler, suv * p.layerTiling[0]);
    float4 c1 = gLayer1.Sample(gSampler, suv * p.layerTiling[1]);
    float4 c2 = gLayer2.Sample(gSampler, suv * p.layerTiling[2]);
    float4 c3 = gLayer3.Sample(gSampler, suv * p.layerTiling[3]);

    float4 final = c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a;

    PS_PRBOutput output;
    output.AlbedoAO = float4(final.rgb, 1.0f);
    output.EmissionMetallic = float4(0, 0, 0, 0);
    output.NormalRoughness = float4(normalize(i.nrm) * 0.5f + 0.5f, 1.0f);

    return output;
}
