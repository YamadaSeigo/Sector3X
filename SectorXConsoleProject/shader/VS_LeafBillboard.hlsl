#include "_LeafParticles.hlsli"

StructuredBuffer<LeafParticle> gParticles : register(t0);
StructuredBuffer<uint> gAlive : register(t1);
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t2);

cbuffer CBCamera : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamRightWS;
    float gBaseSize; // billboard half-size 例: 0.05
    float3 gCamUpWS;
    float gTime;
};

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
};

static const float2 kCornerCCW[4] =
{
    float2(-1, -1),
    float2(1, -1),
    float2(-1, 1),
    float2(1, 1),
};

// tri list: (0,1,2) (2,1,3)
static const uint kVidToCorner[6] = { 0, 1, 2, 2, 1, 3 };

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;

    uint pid = gAlive[iid];
    LeafParticle p = gParticles[pid];
    LeafVolumeGPU v = gVolumes[p.volumeSlot];

    uint c = kVidToCorner[vid];
    float2 s = kCornerCCW[c];

    float size = gBaseSize + p.size;

    float3 worldPos =
        p.posWS +
        gCamRightWS * (s.x * size) +
        gCamUpWS * (s.y * size);

    o.posH = mul(gViewProj, float4(worldPos, 1));

    // uvは単純マップ
    o.uv = (s * 0.5f + 0.5f);

    o.col = v.color * v.intensity;

    return o;
}