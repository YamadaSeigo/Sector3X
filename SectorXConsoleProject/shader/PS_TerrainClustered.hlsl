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
uint ComputeClusterId(float2 worldXZ)
{
    float2 rel = (worldXZ - gOriginXZ) / gCellSizeXZ;
    int2 ij = int2(floor(rel + 1e-6)); // 浮動小数の境界誤差対策
    ij = clamp(ij, int2(0, 0), int2(int(gDimX) - 1, int(gDimZ) - 1));
    return uint(ij.y) * gDimX + uint(ij.x);
}

// セル内 0..1 の局所UVを作る
float2 ComputeClusterLocalUV(float2 worldXZ)
{
    float2 rel = (worldXZ - gOriginXZ) / gCellSizeXZ; // 何セル目か
    float2 fracUV = frac(rel); // 0..1
    // 端の境界処理が気になるなら微小オフセットで 1.0 にならないようにする等の調整可
    return fracUV;
}

float4 NormalizeWeights(float4 w)
{
    w = saturate(w);
    float s = max(1e-5, w.r + w.g + w.b + w.a);
    return w / s;
}

// === PS（ワンドロー本体） ===
float4 main(VSOut i) : SV_Target
{
    // ピクセルの worldPos.xz → クラスタID
    uint cid = ComputeClusterId(i.worldPos.xz);
    ClusterParam p = gClusters[cid];

   // 1) クラスタ局所UV 0..1
    float2 uvC = ComputeClusterLocalUV(i.worldPos.xz);

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

    uint cascade = ChooseCascade(i.viewDepth);

    float4 shadowPos = mul(gLightViewProj[cascade], float4(i.worldPos, 1.0f));

    float shadow = DebugShadowDepth(i.worldPos, cascade);

    //float shadow = SampleShadow(i.worldPos, i.viewDepth);

    float shadowBias = 1.0f;
    if (shadowPos.z - shadow > 0.1f)
        shadowBias = 0.5f;
    //if (shadow < 0.1f)

    return (c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a) * shadowBias;
}
