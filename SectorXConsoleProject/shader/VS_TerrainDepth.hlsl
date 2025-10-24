// TerrainDepthVS.hlsl
#include "TerrainShared.hlsli"

// VS 入力：float2 uv : TEXCOORD0
struct VSIn
{
    float2 uv : TEXCOORD0;
};
struct VSOut
{
    float4 pos : SV_Position;
};

float SampleHeightWS(float2 worldXZ)
{
    // worldXZ→高さUV 変換はあなたのスケールに合わせて
    // ここでは 1:1 の仮実装
    float2 uv = worldXZ; // 必要ならスケール/オフセット
    return gHeight.SampleLevel(gSamp, uv, 0);
}

VSOut main(VSIn i, uint instId : SV_InstanceID)
{
    VSOut o;

    // 可視リストからインスタンスID→クラスタID
    uint clusterId = gVisList.Load(instId);

    ClusterInfo c = gClusters[clusterId];

    float2 xz = c.originWS.xz + i.uv * c.scaleWS;
    float y = SampleHeightWS(xz);

    float3 ws = float3(xz.x, y, xz.y);
    o.pos = mul(gViewProj, float4(ws, 1));
    return o;
}