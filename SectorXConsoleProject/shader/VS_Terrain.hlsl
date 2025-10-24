// TerrainVS.hlsl
#include "ClusterShared.hlsli"

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
    uint vid : SV_VertexID;
};

struct VSOut
{
    float4 clip : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
};


StructuredBuffer<uint> gVisibleList : register(t0); // バケツ固有の可視インデックス
StructuredBuffer<ClusterInfo> gClustersSRV : register(t1); // 共有
// 頂点/インデックスは通常の VB/IB を IA にセット

VSOut main(VSIn vin, uint instId : SV_InstanceID)
{
    VSOut o;
    // クラスターを特定
    uint clusterIndex = gVisibleList[instId];
    ClusterInfo ci = gClustersSRV[clusterIndex];

    // 地形は “共通のワールド = 単位” 前提（必要ならタイルワールド変換を足す）
    float4 wpos = float4(vin.pos, 1);
    float4 cpos = mul(uViewProj, wpos);

    o.clip = cpos;
    o.nrm = vin.nrm;
    o.uv = vin.uv;
    return o;
}
