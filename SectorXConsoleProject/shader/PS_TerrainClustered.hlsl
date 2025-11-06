// t15: GPU 側とレイアウト完全一致
struct ClusterParam
{
    int splatSlice; // Texture2DArray のスライス番号
    int _pad0[3];
    float2 layerTiling[4]; // 各素材のタイル(U,V)
    float2 splatST; // スプラットUVスケール
    float2 splatOffset; // スプラットUVオフセット
};

// ====== レジスタ割り当て（これまでの設計に準拠） ======
Texture2D gLayer0 : register(t10);
Texture2D gLayer1 : register(t11);
Texture2D gLayer2 : register(t12);
Texture2D gLayer3 : register(t13);
Texture2DArray gSplat : register(t14); // クラスタごとの重み (RGBA) を slice で参照
StructuredBuffer<ClusterParam> gClusters : register(t15); // 全クラスタのパラメータ表

SamplerState gSamp : register(s0);

// b2: グリッド定数（DX11BlockRevertHelper::TerrainGridCB と一致）
cbuffer TerrainGridCB : register(b2)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gCellSizeXZ; // 1クラスタのサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    uint _pad0;
    uint _pad1;
};

// VS 出力（worldPos を追加）
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0; // 地形の基礎UV（0..1）
    float3 worldPos : TEXCOORD1; // 少なくとも x,z を使用
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

float4 NormalizeWeights(float4 w)
{
    w = saturate(w);
    float s = max(1e-5, w.r + w.g + w.b + w.a);
    return w / s;
}

// === PS（ワンドロー本体） ===
float4 main(VSOut i) : SV_Target
{
    // 1) ピクセルの worldPos.xz → クラスタID
    uint cid = ComputeClusterId(i.worldPos.xz);
    ClusterParam p = gClusters[cid];

    // 2) スプラット重み（RGBA）を slice 指定で取得
    float2 suv = i.uv * p.splatST + p.splatOffset;
    float4 w = NormalizeWeights(gSplat.Sample(gSamp, float3(suv, p.splatSlice)));

    // 3) 共通素材4枚をタイルサンプル
    float4 c0 = gLayer0.Sample(gSamp, i.uv * p.layerTiling[0]);
    float4 c1 = gLayer1.Sample(gSamp, i.uv * p.layerTiling[1]);
    float4 c2 = gLayer2.Sample(gSamp, i.uv * p.layerTiling[2]);
    float4 c3 = gLayer3.Sample(gSamp, i.uv * p.layerTiling[3]);

    // 4) 重みブレンド
    return c0 * w.r + c1 * w.g + c2 * w.b + c3 * w.a;
}
