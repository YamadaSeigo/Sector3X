// CS_LeafClumpUpdate_GroundFollow.hlsl
// 1 thread = 1 clump
// Updates: s, laneCenter/radialCenter, yOffset (ground-follow)

#include "_LeafParticles.hlsli"

#define LEAF_CLUMP_HAS_YVEL

// ----------------------------
// SRVs
// ----------------------------
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);
Texture2D<float> gHeightMap : register(t1);
SamplerState gHeightSamp : register(s0);

StructuredBuffer<LeafGuideCurve> gCurves : register(t2);


// ----------------------------
// UAVs
// ----------------------------
RWStructuredBuffer<LeafClump> gClumps : register(u0);

// ----------------------------
// Constant buffers
// ----------------------------
cbuffer CBClumpUpdate : register(b0)
{
    float gDt;
    float gTime;
    uint gActiveVolumeCount;
    uint gClumpsPerVolume;

    uint gCurvesPerVolume; // 0なら全体共有とみなす
    float gClumpLength01; // 粒子側で使う: cl.s からの前後散らし幅（ここでは使用しない）

    // ---- Swarm / wobble ----
    float gClumpLaneAmp; // 例: 0.2～1.0（radiusに対して）
    float gClumpRadialAmp; // 例: 0.05～0.3
    float gClumpLaneFreq; // 例: 0.7
    float gClumpRadialFreq; // 例: 0.9

    // ---- Ground follow ----
    float gGroundBase; // 例: 0.25 (m above ground)
    float gGroundWaveAmp; // 例: 0.35
    float gGroundWaveFreq; // 例: 0.8
    float gGroundFollowK; // 例: 6.0 (spring strength)
    float gGroundFollowD; // 例: 1.2 (damping on y-velocity)

    // ---- Limits ----
    float gLaneLimitScale; // 例: 1.0 (lane limit = radius*scale)
    float gRadialLimitScale; // 例: 0.5
    float gMaxYOffset; // 例: 5.0 (safety clamp)

    float2 pad;
};


// Terrain sampling info
cbuffer TerrainGridCB : register(b1)
{
    float2 gOriginXZ;
    float2 gClusterXZ;
    uint gDimX;
    uint gDimZ;
    float heightScale;
    float offsetY;

    uint gVertsX;
    uint gVertsZ;

    uint2 padding;
    float2 gCellSize;
    float2 gHeightMapInvSize;
};

cbuffer WindCB : register(b2)
{
    float gWindTime;
    float gNoiseFreq;
    float gBigWaveWeight;
    float gWindSpeed;
    float gWindAmplitude;
    float3 gWindDir; // XZ推奨（normalize済み推奨）
};

// ----------------------------
// Helpers
// ----------------------------
uint Hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}
float Hash01(uint x)
{
    return (Hash_u32(x) & 0x00FFFFFFu) / 16777216.0f;
}
float2 SafeN2(float2 v)
{
    float l = length(v);
    return (l > 1e-6) ? (v / l) : float2(1, 0);
}
float3 SafeN(float3 v)
{
    float l = length(v);
    return (l > 1e-6) ? (v / l) : float3(0, 1, 0);
}

float SampleGroundY(float2 xz)
{
    float2 terrainSize = gClusterXZ * float2(gDimX, gDimZ);
    float2 uv = saturate((xz - gOriginXZ) / max(terrainSize, float2(1e-3, 1e-3)));
    float h = gHeightMap.SampleLevel(gHeightSamp, uv, 0);
    return h * heightScale + offsetY;
}

float3 Bezier3(float3 p0, float3 p1, float3 p2, float3 p3, float t)
{
    float u = 1.0 - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;
    return uuu * p0 + 3.0 * uu * t * p1 + 3.0 * u * tt * p2 + ttt * p3;
}

float3 LocalToWorld(float3 local, float3 right, float3 up, float3 fwd)
{
    return right * local.x + up * local.y + fwd * local.z;
}

// ----------------------------
// Main
// ----------------------------
#ifndef CLUMP_THREAD_GROUP_SIZE
#define CLUMP_THREAD_GROUP_SIZE 256
#endif

[numthreads(CLUMP_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint clumpId = tid.x;
    uint totalClumps = gActiveVolumeCount * gClumpsPerVolume;
    if (clumpId >= totalClumps)
        return;

    uint volIdx = clumpId / max(gClumpsPerVolume, 1u);

    LeafVolumeGPU vol = gVolumes[volIdx];
    LeafClump cl = gClumps[clumpId];

    // ----------------------------
    // Wind basis (XZ)  ※地面追従の水平オフセット用
    // ----------------------------
    float3 up = float3(0, 1, 0);

    // forward is wind dir on XZ
    float3 fwd = SafeN(float3(gWindDir.x, 0.0f, gWindDir.z));
    if (abs(dot(fwd, up)) > 0.98)
        fwd = float3(0, 0, 1);

    float3 right3 = SafeN(cross(up, fwd));
    float3 binorm3 = SafeN(cross(fwd, right3));

    float2 rightXZ = SafeN2(right3.xz);
    float2 binormXZ = SafeN2(binorm3.xz);

    // ----------------------------
    // Curve selection (keep inside volume range if needed)
    // ----------------------------
    uint curveId = cl.curveId;
    if (gCurvesPerVolume != 0)
    {
        uint base = volIdx * gCurvesPerVolume;
        uint local = (curveId - base) % gCurvesPerVolume;
        curveId = base + local;
    }

    LeafGuideCurve c = gCurves[curveId];

    // ----------------------------
    // Advance s (normalized by curve length)
    // ----------------------------
    float len = max(c.lengthApprox, 0.001f);
    float speedAlong = max(vol.speed, 0.0f) * gWindAmplitude * max(cl.speedMul, 0.0f);
    cl.s = frac(cl.s + (speedAlong * gDt / len));

    // ----------------------------
    // Swarm wobble (lane/radial centers)
    //  ※clump全体の“うねり”。粒子個体差は別途 p.lane/p.radial で小さく散らす
    // ----------------------------
    // clumpごとに少しfreqをずらす（同調しすぎ防止）
    float fLane = gClumpLaneFreq * lerp(0.8, 1.2, Hash01(cl.seed ^ 0xA1u));
    float fRadial = gClumpRadialFreq * lerp(0.8, 1.2, Hash01(cl.seed ^ 0xB2u));

    cl.laneCenter += sin(gTime * fLane + cl.phase) * gClumpLaneAmp * gDt;
    cl.radialCenter += sin(gTime * fRadial + cl.phase * 1.3f) * gClumpRadialAmp * gDt;

    // clamp inside volume-ish
    float laneLim = vol.radius * max(gLaneLimitScale, 0.0f);
    float radLim = vol.radius * max(gRadialLimitScale, 0.0f);
    cl.laneCenter = clamp(cl.laneCenter, -laneLim, laneLim);
    cl.radialCenter = clamp(cl.radialCenter, -radLim, radLim);

    // ----------------------------
    // Compute curve center (local->world) for this clump
    // ----------------------------
    float3 centerL = Bezier3(c.p0L, c.p1L, c.p2L, c.p3L, cl.s);
    float3 centerWS = vol.centerWS + LocalToWorld(centerL, right3, up, fwd);

    // ----------------------------
    // Ground sample at *offset-applied XZ* (IMPORTANT)
    // ----------------------------
    float2 offsetXZ = rightXZ * cl.laneCenter + binormXZ * cl.radialCenter;
    float2 sampleXZ = centerWS.xz + offsetXZ;

    float groundY = SampleGroundY(sampleXZ);

    // Desired height above ground + gentle wave
    float wave = sin(gTime * (6.2831853f * gGroundWaveFreq) + cl.phase) * gGroundWaveAmp;
    float desiredY = groundY + gGroundBase + wave;

    // We control y via yOffset: (centerWS.y + yOffset) -> desiredY
    float targetOffset = desiredY - centerWS.y;

    // ----------------------------
    // yOffset spring-damper (stable follow)
    // ----------------------------
    // If you don't have cl.yVel, add it to LeafClump.
    // ここでは cl.yVel を持っている前提の例（持ってないなら後述）。
    // LeafClump に "float yVel;" を追加推奨。
    // ------------------------------------
#if defined(LEAF_CLUMP_HAS_YVEL)
    float y = cl.yOffset;
    float vY = cl.yVel;

    float err = (targetOffset - y);
    // spring: a = k*err - d*v
    float a = gGroundFollowK * err - gGroundFollowD * vY;
    vY += a * gDt;
    y  += vY * gDt;

    cl.yVel = vY;
    cl.yOffset = clamp(y, -gMaxYOffset, gMaxYOffset);
#else
    // yVelを持たない簡易版（lerp追従）
    float t = saturate(gGroundFollowK * gDt);
    cl.yOffset = lerp(cl.yOffset, targetOffset, t);
    cl.yOffset = clamp(cl.yOffset, -gMaxYOffset, gMaxYOffset);
#endif

    float2 windXZ = normalize(gWindDir.xz);
    float2 drift = windXZ * (0.6f * vol.speed) + Hash01(cl.seed ^ 0xC1u) * 0.3f;

    cl.anchorVelXZ += drift * gDt;
    cl.anchorVelXZ *= exp(-0.98f * gDt);

    cl.anchorXZ += cl.anchorVelXZ * gDt;

    // 半径内に保つ（弱いバネ）
    float r = length(cl.anchorXZ);
    if (r > vol.radius)
        cl.anchorXZ *= (vol.radius / r);


    // store curveId after remap (optional)
    cl.curveId = curveId;

    gClumps[clumpId] = cl;
}
