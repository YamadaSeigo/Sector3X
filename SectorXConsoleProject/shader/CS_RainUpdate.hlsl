#include "_RainParticles.hlsli"

// AliveIn は SRV で読む
StructuredBuffer<uint> gAliveIn : register(t0);

// aliveCount は raw buffer
ByteAddressBuffer gAliveCountRaw : register(t1);

// 深度テクスチャ（サンプリングして衝突判定に使う）
Texture2D<float> gRainDepth : register(t2);

// particles
RWStructuredBuffer<RainParticle> gParticles : register(u0);

// AliveOut は append UAV
AppendStructuredBuffer<uint> gAliveOut : register(u1);

// FreeList は append UAV（返却用）
AppendStructuredBuffer<uint> gFreeList : register(u2);

SamplerState gPointClamp : register(s0);

cbuffer CBUpdate : register(b0)
{
    float gDt;
    float gTime;
    float gGravity;

    float padding0;

    float3 gCamPosWS;
    float gSpawnRadius;

    float3 gWindWS;

    float padding1;

    float2 gRainInvMapSize; // (1/width, 1/height)
    float gRainDepthBias;
    float padding2;
};

cbuffer CBMatrix : register(b1)
{
    row_major float4x4 gCamViewProj;
};

// 軽いハッシュ乱数（0..1）
uint Hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

float3 HashDir3(uint s)
{
    float x = (Hash_u32(s * 1664525u + 1013904223u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float y = (Hash_u32(s * 22695477u + 1u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float z = (Hash_u32(s * 1103515245u + 12345u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    return normalize(float3(x, y, z));
}

bool IsUnderOccluder(float3 posWS)
{
    float4 h = mul(gCamViewProj, float4(posWS, 1));
    float3 ndc = h.xyz / h.w;

    // ndc.xy: -1..1 -> uv 0..1
    float2 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = -ndc.y * 0.5f + 0.5f;

    // マップ外は判定しない（=雨は降らせる）など運用で選ぶ
    if (any(uv < 0) || any(uv > 1))
        return false;

    float occ = gRainDepth.SampleLevel(gPointClamp, uv, 0);

    // ndc.z を depth と同じ空間に
    float z = ndc.z; // 例

    return (z > occ + gRainDepthBias);
}

bool NeedsRespawn(RainParticle p)
{
    if (p.life <= 0.0f)
        return true;

    // Horizontal distance from camera (XZ in camera-right/up plane is more stable; but simple WS is OK)
    float3 d = p.posWS - gCamPosWS;
    float dist2 = dot(d.xz, d.xz);
    if (dist2 > (gSpawnRadius * gSpawnRadius))
        return true;

#ifndef DEBUG_RAIN_HIT_DEPTH
    if (IsUnderOccluder(p.posWS))
        return true;
#endif

    // NaN guard
    if (any(p.posWS != p.posWS))
        return true;
    if (any(p.velWS != p.velWS))
        return true;

    return false;
}

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);
    uint i = tid.x;
    if (i >= aliveCount)
        return;

    uint id = gAliveIn[i];

    RainParticle p = gParticles[id];

     // Integrate
    p.life -= gDt;

    // Gravity + wind (wind already in vel; still allow gust changes)
    // Apply gravity as acceleration on Y.
    p.velWS.y -= gGravity * gDt;

    // Optionally, steer horizontal velocity toward current wind (for changing wind)
    p.velWS.xz = lerp(p.velWS.xz, gWindWS.xz, saturate(2.0f * gDt));

    p.posWS += p.velWS * gDt;

    if (NeedsRespawn(p))
    {
         // FreeListへ返す
        gFreeList.Append(id);
        return;
    }

#ifdef DEBUG_RAIN_HIT_DEPTH
    if (IsUnderOccluder(p.posWS))
    {
        p.debugHit = 255;
    }

    p.debugHit = (p.debugHit > 0) ? (p.debugHit - 8) : 0; // 0.5秒くらいで消える感じ
#endif

    gParticles[id] = p;
    gAliveOut.Append(id);
}
