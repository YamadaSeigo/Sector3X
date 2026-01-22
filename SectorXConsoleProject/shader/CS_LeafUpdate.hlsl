// CS_LeafUpdate_GuideCurve.hlsl
// Firefly方式: AliveIn(SRV) + AliveCountRaw + Particles(UAV) + AliveOut(Append) + FreeList(Append)

#include "_LeafParticles.hlsli"


// ----------------------------
// Resources (match Firefly style)
// ----------------------------
// SRVs
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);
StructuredBuffer<uint> gAliveIn : register(t1);
ByteAddressBuffer gAliveCountRaw : register(t2);

// Optional: heightmap SRV if you want ground constraint (not used in core code)
 Texture2D<float> gHeightMap : register(t3);
 SamplerState     gHeightSamp : register(s0);

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
};

// 地形グリッド情報
cbuffer TerrainGridCB : register(b1)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z) ※同上
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)

    uint2 padding; // 未使用

    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height
};

cbuffer WindCB : register(b2)
{
    float gWindTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float3 gWindDir; // XZ 平面の風向き (正規化済み)
};

cbuffer CBParams : register(b3)
{
    float gKillRadiusScale; // e.g. 1.5
    float gDamping; // e.g. 0.96 (NOTE: multiply style)
    float gFollowK; // lateral steer strength (e.g. 6..14)
    float gMaxSpeed; // e.g. 6.0

    // ---- Ground follow params (NEW) ----
    float gGroundBase; // e.g. 0.25  (meters above ground)
    float gGroundWaveAmp; // e.g. 0.35  (meters)
    float gGroundWaveFreq; // e.g. 0.8   (Hz-ish)
    float gGroundFollowK; // e.g. 2.0   (y position spring)
    float gGroundFollowD; // e.g. 1.2   (y velocity damping)
    
    float3 padParams;
};

float SampleGroundY(float2 xz)
{
    float2 terrainSize = gClusterXZ * float2(gDimX, gDimZ);
    float2 uv = saturate((xz - gOriginXZ) / terrainSize);
    float h = gHeightMap.SampleLevel(gHeightSamp, uv, 0);
    return h * heightScale + offsetY;
}

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

    LeafVolumeGPU vol = gVolumes[p.volumeSlot];

    float dt = gDt;

    // --- Lifetime ---
    p.life -= dt;
    if (p.life <= 0.0f)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // --- Wind basis ---
    float3 up = float3(0, 1, 0);

    // Use XZ wind to avoid vertical degeneracy
    float3 fwd = SafeN(float3(gWindDir.x, 0.0f, gWindDir.z));
    if (abs(dot(fwd, up)) > 0.98)
        fwd = float3(0, 0, 1);

    float3 right = SafeN(cross(up, fwd));
    float3 binorm = SafeN(cross(fwd, right));

    // --- Curve eval (LOCAL) ---
    uint curveId = p.curveId; // ※安全化するなら % gCurveCount
    LeafGuideCurve c = gCurves[curveId];

    float len = max(c.lengthApprox, 0.001);
    float speedAlong = max(vol.speed, 0.0) * gWindAmplitude;
    float speedVar = lerp(0.8, 1.2, Hash01(id * 9781u + 17u));
    p.s = frac(p.s + (speedAlong * speedVar) * dt / len);

    float3 centerL = Bezier3(c.p0L, c.p1L, c.p2L, c.p3L, p.s);
    float3 tanL = Bezier3Tangent(c.p0L, c.p1L, c.p2L, c.p3L, p.s);
    float3 tanWS = SafeN(LocalToWorld(tanL, right, up, fwd));

    // --- Curve cross-section basis ---
    float3 curveRightWS = SafeN(cross(up, tanWS));
    // fallback if degenerate
    if (dot(curveRightWS, curveRightWS) < 1e-6)
        curveRightWS = right;

    float3 curveBinormWS = SafeN(cross(tanWS, curveRightWS));
    if (dot(curveBinormWS, curveBinormWS) < 1e-6)
        curveBinormWS = up;

    // thickness offsets
    float3 offsetWS = curveRightWS * p.lane + curveBinormWS * p.radial;

    // center in world
    float3 centerWS = vol.centerWS + LocalToWorld(centerL, right, up, fwd);
    float3 targetWS = centerWS + offsetWS;

    // --- Ground-follow height (NEW) ---
    // Make Y follow "ground + base + gentle wave + radial"
    float groundY = SampleGroundY(p.posWS.xz);

    // gentle wave (per-particle phase)  ※radialは上下の厚みとして維持
    float wave = sin(gTime * (6.2831853f * gGroundWaveFreq) + p.phase) * gGroundWaveAmp;

    float targetY = groundY + gGroundBase + wave + p.radial;
    float dy = targetY - p.posWS.y;

    // PD-ish on Y (soft)
    // K: position -> accel, D: damp vertical velocity
    float yAccel = dy * gGroundFollowK - p.velWS.y * gGroundFollowD;
    p.velWS.y += yAccel * dt;

    // prevent going under ground
    p.posWS.y = max(p.posWS.y, groundY + 0.02f);

    // --- Steering (lateral only) ---
    float3 toTarget = targetWS - p.posWS;

    float3 lateral = toTarget - tanWS * dot(toTarget, tanWS);

    // Optional: avoid strong vertical "pull to curve"; let ground-follow handle Y
    lateral.y = 0.0f;

    float3 steer = lateral * gFollowK;

    // flow along tangent
    float3 flow = tanWS * speedAlong;

    // --- Flutter ---
    uint nseed = Hash_u32(id ^ (uint) (gTime * 60.0));
    float3 flutterDir = HashDir3(nseed);
    flutterDir.y *= 0.35;
    float3 flutter = flutterDir * (0.05f * vol.noiseScale);

    // --- Player repel ---
    float3 repel = 0;
    float3 toP = p.posWS - gPlayerPosWS;
    float dP = length(toP);
    if (dP < gPlayerRepelRadius && dP > 1e-3)
    {
        float w = 1.0 - (dP / gPlayerRepelRadius);
        repel = (toP / dP) * (w * 6.0);
    }

    // --- Integrate ---
    float3 accel = steer + flutter + repel;
    p.velWS += accel * dt;

    // Align toward flow
    p.velWS = lerp(p.velWS, flow, saturate(0.08 + 0.12 * vol.noiseScale));

    // Damping (multiply style; if you want dt-aware, use exp(-k*dt))
    p.velWS *= gDamping;

    // Clamp speed
    float sp = length(p.velWS);
    if (sp > gMaxSpeed)
        p.velWS *= (gMaxSpeed / max(sp, 1e-6));

    // Position
    p.posWS += p.velWS * dt;

    // Kill if too far
    float3 dv = p.posWS - vol.centerWS;
    float dist = length(dv);
    float killR = vol.radius * max(gKillRadiusScale, 1.0);
    if (dist > killR)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // phase
    p.phase += dt;

    gParticles[id] = p;
    gAliveOut.Append(id);
}
