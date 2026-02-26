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

Texture2D gLayerNormal0 : register(t24);
Texture2D gLayerNormal1 : register(t25);
Texture2D gLayerNormal2 : register(t26);
Texture2D gLayerNormal3 : register(t27);


Texture2DArray gSplat : register(t28); // クラスタごとの重み (RGBA) を slice で参照
StructuredBuffer<ClusterParam> gClusters : register(t29); // 全クラスタのパラメータ表

Texture2DArray gBiome : register(t30); // RGBA = (Desert, Swamp, Forest, Tundra) など

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

float3 DecodeNormal(Texture2D tex, float2 uv)
{
    float2 xy = tex.Sample(gSampler, uv).rg * 2.0f - 1.0f;
    float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy, z);
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

    float2 uv0 = suv * p.layerTiling[0];
    float2 uv1 = suv * p.layerTiling[1];
    float2 uv2 = suv * p.layerTiling[2];
    float2 uv3 = suv * p.layerTiling[3];

    float4 c0 = gLayer0.Sample(gSampler, uv0);
    float4 c1 = gLayer1.Sample(gSampler, uv1);
    float4 c2 = gLayer2.Sample(gSampler, uv2);
    float4 c3 = gLayer3.Sample(gSampler, uv3);

    float4 final = c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a;

    // バイオームごとに色味を変える例
    float4 b = gBiome.SampleLevel(gPointClamp, float3(suv, p.splatSlice), 0);

    float4 biomeW = b; // 例: (desert, swamp, forest, tundra)
    biomeW /= max(1e-5, dot(biomeW, 1)); // 正規化（必要なら）

    float3 tintDesert = float3(2.10, 0.8f, 0.8);
    float3 tintSwamp = float3(0.85, 0.5f, 2.0f);
    float3 tintForest = float3(0.95, 1.05, 0.95);
    float3 tintTundra = float3(0.95, 0.98, 1.05);

    float3 tint =
    tintDesert * biomeW.r +
    tintSwamp * biomeW.g +
    tintForest * biomeW.b +
    tintTundra * biomeW.a;

    final.rgb *= tint;


    PS_PRBOutput output;
    output.AlbedoAO = float4(final.rgb, 1.0f);
    output.EmissionMetallic = float4(0, 0, 0, 0);


    //　ここから法線計算
    float3 n0 = DecodeNormal(gLayerNormal0, uv0);
    float3 n1 = DecodeNormal(gLayerNormal1, uv1);
    float3 n2 = DecodeNormal(gLayerNormal2, uv2);
    float3 n3 = DecodeNormal(gLayerNormal3, uv3);

    float3 nTS =
      n0 * w.r
    + n1 * w.g
    + n2 * w.b
    + n3 * w.a;

    nTS = normalize(nTS);

    float3 N = normalize(i.nrm);

    // ワールドUp
    float3 up = float3(0, 1, 0);

    // 法線が垂直に近い場合の安全処理
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    float3 nWS = normalize(mul(nTS, TBN));

    output.NormalRoughness = float4(nWS * 0.5f + 0.5f, 0.6f);

    return output;
}
