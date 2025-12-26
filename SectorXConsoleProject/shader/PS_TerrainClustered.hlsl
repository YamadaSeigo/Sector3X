#include "_GlobalTypes.hlsli"

// t25: GPU 側とレイアウト完全一致
struct ClusterParam
{
    int splatSlice; // Texture2DArray のスライス番号
    int _pad0[3];
    float2 layerTiling[4]; // 各素材のタイル(U,V)
    float2 splatST; // スプラットUVスケール
    float2 splatOffset; // スプラットUVオフセット
};

// ====== レジスタ割り当て（これまでの設計に準拠） ======
Texture2D gLayer0 : register(t20);
Texture2D gLayer1 : register(t21);
Texture2D gLayer2 : register(t22);
Texture2D gLayer3 : register(t23);
Texture2DArray gSplat : register(t24); // クラスタごとの重み (RGBA) を slice で参照
StructuredBuffer<ClusterParam> gClusters : register(t25); // 全クラスタのパラメータ表

// b14: グリッド定数（DX11BlockRevertHelper::TerrainGridCB と一致）
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gCellSizeXZ; // 1クラスタのサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    uint _pad00;
    uint _pad11;
};

// VS 出力（worldPos を追加）
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0; // 地形の基礎UV（0..1）
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

    float2 rel = (worldXZ - gOriginXZ) / gCellSizeXZ;

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

// === PS（ワンドロー本体） ===
PS_PRBOutput main(VSOut i)
{
    // ピクセルの worldPos.xz -> クラスタID
    ClusterCoord c = ComputeCluster(i.worldPos.xz);
    uint cid = c.id;
    float2 uvC = c.localUV;

    ClusterParam p = gClusters[cid];

    // 2) スプラット重み：Texture2DArray なら slice 指定
    //    通常は p.splatST=(1,1), p.splatOffset=(0,0) 固定でOK
    float2 suv = uvC * p.splatST + p.splatOffset;
    float4 w = gSplat.Sample(gSampler, float3(suv, p.splatSlice));
    w = saturate(w);
    w /= max(1e-5, dot(w, 1));


    // 3) 素材4：連続感が欲しければ world ベースでタイルするのがおすすめ
    //    例: ワールドXZをスケール（完全にクラスタ無関係の連続タイル）
    //float2 uvWorld = i.worldPos.xz; // 必要に応じて / overallScale
    float4 c0 = gLayer0.Sample(gSampler, suv * p.layerTiling[0]);
    float4 c1 = gLayer1.Sample(gSampler, suv * p.layerTiling[1]);
    float4 c2 = gLayer2.Sample(gSampler, suv * p.layerTiling[2]);
    float4 c3 = gLayer3.Sample(gSampler, suv * p.layerTiling[3]);

    //uint cascade = ChooseCascade(i.viewDepth);

    //float4 shadowPos = mul(gLightViewProj[cascade], float4(i.worldPos, 1.0f));

    //float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

    //float shadowBias = 1.0f;
    //if (shadowPos.z - shadow > 0.001f)
    //    shadowBias = 0.7f;

    float4 final = c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a;
    //final.rgb *= shadowBias;

    PS_PRBOutput output;
    output.AlbedoAO = float4(final.rgb, 1.0f);
    output.EmissionMetallic = float4(0, 0, 0, 0);
    output.NormalRoughness = float4(normalize(i.nrm) * 0.5f + 0.5f, 1.0f);

    return output;
}
