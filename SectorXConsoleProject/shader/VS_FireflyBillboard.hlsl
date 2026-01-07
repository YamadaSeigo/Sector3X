#include "_FireflyParticles.hlsli"

StructuredBuffer<FireflyParticle> gParticles : register(t0);
StructuredBuffer<uint> gAlive : register(t1);
StructuredBuffer<FireflyVolumeGPU> gVolumes : register(t2);

cbuffer CBCamera : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamRightWS;
    float gSize; // billboard half-size 例: 0.05
    float3 gCamUpWS;
    float gTime;
};

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : TEXCOORD1;
    float a : TEXCOORD2;
};

static const float2 kCornerCCW[4] =
{
    float2(-1, -1),
    float2( 1, -1),
    float2(-1,  1),
    float2( 1,  1),
};

// tri list: (0,1,2) (2,1,3)
static const uint kVidToCorner[6] = { 0, 1, 2, 2, 1, 3 };

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;

    uint pid = gAlive[iid];
    FireflyParticle p = gParticles[pid];
    FireflyVolumeGPU v = gVolumes[p.volumeSlot];

    uint c = kVidToCorner[vid];
    float2 s = kCornerCCW[c];

    float3 worldPos =
        p.posWS +
        gCamRightWS * (s.x * gSize) +
        gCamUpWS * (s.y * gSize);

    o.posH = mul(gViewProj, float4(worldPos, 1));

    // uvは単純マップ
    o.uv = (s * 0.5f + 0.5f);

    // 点滅（phase + time）
    float blink = 0.5f + 0.5f * sin(gTime * 6.0f + p.phase);

    o.col = v.color * v.intensity;
    o.a = blink;

    return o;
}