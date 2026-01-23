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
    // corner
    float2 s = kCornerCCW[c];

    float2 v2 = float2(dot(p.velWS, gCamRightWS), dot(p.velWS, gCamUpWS));
    float vlen2 = dot(v2, v2);
    float velAngle = (vlen2 > 1e-6) ? atan2(v2.y, v2.x) : 0.0f;

    // ランダム回転（phase）と速度方向をブレンド
    float angle = lerp(p.phase, velAngle, 0.7f); // 0.0=ランダム優先, 1.0=速度優先

    float sn, cs;
    sincos(angle, sn, cs);

    // ビルボード平面内で回転
    float2 sr;
    sr.x = s.x * cs - s.y * sn;
    sr.y = s.x * sn + s.y * cs;

    float size = gBaseSize + p.size;

    float3 worldPos =
    p.posWS +
    gCamRightWS * (sr.x * size) +
    gCamUpWS * (sr.y * size);

    o.posH = mul(gViewProj, float4(worldPos, 1));

    // uvは単純マップ
    o.uv = (s * 0.5f + 0.5f);

    o.col = v.color * v.intensity;

    return o;
}