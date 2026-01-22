// CS_LeafUpdate_GuideCurve.hlsl
// Firefly方式: AliveIn(SRV) + AliveCountRaw + Particles(UAV) + AliveOut(Append) + FreeList(Append)

#include "_LeafParticles.hlsli"


// Guide curve is stored in LOCAL space (volume-centered basis space):
// x = right, y = up, z = forward (wind direction)
struct LeafGuideCurve
{
    float3 p0L;
    float3 p1L;
    float3 p2L;
    float3 p3L;
    float lengthApprox; // used for ds/dt normalization
};

// ----------------------------
// Resources (match Firefly style)
// ----------------------------
// SRVs
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);
StructuredBuffer<uint> gAliveIn : register(t1);
ByteAddressBuffer gAliveCountRaw : register(t2);

// Optional: heightmap SRV if you want ground constraint (not used in core code)
// Texture2D<float> gHeightMap : register(t3);
// SamplerState     gHeightSamp : register(s0);

StructuredBuffer<LeafGuideCurve> gCurves : register(t4);

// UAVs
RWStructuredBuffer<LeafParticle> gParticles : register(u0);
AppendStructuredBuffer<uint> gAliveOut : register(u1); // Append!
AppendStructuredBuffer<uint> gFreeList : register(u2); // Append!
RWStructuredBuffer<uint> gVolumeCount : register(u3);

// ----------------------------
// Constant buffers
// ----------------------------
cbuffer CBUpdate : register(b0)
{
    float gDt;
    float gTime;
    float2 _pad0;

    float3 gPlayerPosWS;
    float gPlayerRepelRadius;

    float gKillRadiusScale; // e.g. 1.5 (kill if dist > radius*scale)
    float gDamping; // e.g. 0.96
    float gFollowK; // e.g. 12.0  (steer strength)
    float gMaxSpeed; // e.g. 6.0
};

cbuffer WindCB : register(b1)
{
    float gWindTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float3 gWindDir; // XZ 平面の風向き (正規化済み)
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

float3 HashDir3(uint s)
{
    float x = Hash01(s * 1664525u + 1013904223u) * 2.0f - 1.0f;
    float y = Hash01(s * 22695477u + 1u) * 2.0f - 1.0f;
    float z = Hash01(s * 1103515245u + 12345u) * 2.0f - 1.0f;
    float3 v = float3(x, y, z);
    float l = max(length(v), 1e-6);
    return v / l;
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

float3 Bezier3Tangent(float3 p0, float3 p1, float3 p2, float3 p3, float t)
{
    float u = 1.0 - t;
    return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

// local (right/up/fwd) -> world
float3 LocalToWorld(float3 local, float3 right, float3 up, float3 fwd)
{
    return right * local.x + up * local.y + fwd * local.z;
}

// safe normalize
float3 SafeN(float3 v)
{
    float l = length(v);
    return (l > 1e-6) ? (v / l) : float3(0, 1, 0);
}

// ----------------------------
// Main
// ----------------------------
[numthreads(LEAF_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);
    uint idx = tid.x;
    if (idx >= aliveCount)
        return;

    uint id = gAliveIn[idx];
    LeafParticle p = gParticles[id];

    // Volume fetch (slot = volumeSlot)
    LeafVolumeGPU vol = gVolumes[p.volumeSlot];

    float dt = gDt;

    // --- Lifetime ---
    p.life -= dt;
    if (p.life <= 0.0f)
    {
        // return to freelist
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // --- Basis from wind dir ---
    float3 up = float3(0, 1, 0);
    float3 fwd = SafeN(gWindDir);
    // if wind almost vertical, make a fallback forward
    if (abs(dot(fwd, up)) > 0.98)
        fwd = float3(0, 0, 1);

    float3 right = SafeN(cross(up, fwd));
    float3 binorm = SafeN(cross(fwd, right));

    // --- Evaluate curve in LOCAL space ---
    uint curveId = p.curveId;
    LeafGuideCurve c = gCurves[curveId];

    // advance s along curve (normalize by lengthApprox)
    float len = max(c.lengthApprox, 0.001);
    float speedAlong = max(vol.speed, 0.0) * gWindAmplitude; // base
    // add per-particle variation
    float speedVar = lerp(0.8, 1.2, Hash01(id * 9781u + 17u));
    p.s = frac(p.s + (speedAlong * speedVar) * dt / len);

    float3 centerL = Bezier3(c.p0L, c.p1L, c.p2L, c.p3L, p.s);
    float3 tanL = Bezier3Tangent(c.p0L, c.p1L, c.p2L, c.p3L, p.s);
    float3 tanWS = SafeN(LocalToWorld(tanL, right, up, fwd));

    // curve-local right/binorm in world (use tangent + global up)
    float3 curveRightWS = SafeN(cross(up, tanWS));
    float3 curveBinormWS = SafeN(cross(tanWS, curveRightWS));

    // thickness offsets (lane/radial) in world
    float3 offsetWS = curveRightWS * p.lane + curveBinormWS * p.radial;

    float3 centerWS = vol.centerWS + LocalToWorld(centerL, right, up, fwd);
    float3 targetWS = centerWS + offsetWS;

    // --- Steering: follow target ---
    float3 toTarget = (targetWS - p.posWS);
    float3 steer = toTarget * gFollowK; // spring-like
    // Add "flow along tangent" to encourage arc motion
    float3 flow = tanWS * speedAlong;

    // --- Flutter noise (small accel) ---
    uint nseed = Hash_u32(id ^ (uint) (gTime * 60.0));
    float3 flutterDir = HashDir3(nseed);
    // keep flutter mostly on plane
    flutterDir.y *= 0.35;
    float3 flutter = flutterDir * ( 0.3f/*gFlutterAmp*/ * vol.noiseScale);

    // --- Player repel (optional) ---
    float3 repel = 0;
    float3 toP = p.posWS - gPlayerPosWS;
    float dP = length(toP);
    if (dP < gPlayerRepelRadius && dP > 1e-3)
    {
        float w = 1.0 - (dP / gPlayerRepelRadius);
        repel = (toP / dP) * (w * 6.0);
    }

    // --- Integrate ---
    // acceleration model: steer + flutter + repel, and gently pull velocity toward flow
    float3 accel = steer + flutter + repel;
    p.velWS += accel * dt;

    // Blend velocity toward flow direction (keeps motion aligned with arc)
    p.velWS = lerp(p.velWS, flow, saturate(0.08 + 0.12 * vol.noiseScale));

    // Damping
    p.velWS *= gDamping;

    // Clamp speed
    float sp = length(p.velWS);
    if (sp > gMaxSpeed)
        p.velWS *= (gMaxSpeed / max(sp, 1e-6));

    // Update position
    p.posWS += p.velWS * dt;

    // --- Optional: ground constraint hook ---
    // ここに Firefly と同じ HeightMap / TerrainCB を使った「地面より少し上に寄せる」処理を挿入できます。
    // p.posWS.y = max(p.posWS.y, groundY + someOffset);

    // --- Kill if out of volume bounds (prevents drifting away forever) ---
    float3 dv = p.posWS - vol.centerWS;
    float dist = length(dv);
    float killR = vol.radius * max(gKillRadiusScale, 1.0);
    if (dist > killR)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // Update phase (for billboard animation)
    p.phase += dt;

    // store + append alive
    gParticles[id] = p;
    gAliveOut.Append(id);
}
