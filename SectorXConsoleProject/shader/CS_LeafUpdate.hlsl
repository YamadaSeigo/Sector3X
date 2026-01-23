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

StructuredBuffer<LeafClump> gClumps : register(t5);

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
    
    // Clump関連
    uint gClumpsPerVolume; // 例: 16
    uint gCurvesPerVolume; // 例: 32（volumeごとに曲線がまとまってる場合）
    uint gTotalClumps; // activeVolumeCount*gClumpsPerVolume（保険用）
    float gClumpLength01; // 例: 0.12（塊の“長さ”= s方向の広がり）
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

    // ---- lifetime ----
    p.life -= dt;
    if (p.life <= 0.0f)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint)-1);
        return;
    }

    // ---- wind basis ----
    float3 up = float3(0, 1, 0);
    float3 fwd = SafeN(float3(gWindDir.x, 0.0f, gWindDir.z));
    if (abs(dot(fwd, up)) > 0.98) fwd = float3(0, 0, 1);

    float3 right = SafeN(cross(up, fwd));
    float3 binorm = SafeN(cross(fwd, right));

    // ============================================================
    // ★ Clump fetch
    // ============================================================
    uint clumpId = p.clumpId;
    // 保険（clumpIdが壊れても落ちにくく）
    if (clumpId >= gTotalClumps)
    {
        // とりあえず volume内に丸める
        clumpId = (uint)p.volumeSlot * gClumpsPerVolume + (clumpId % max(gClumpsPerVolume,1u));
    }
    LeafClump cl = gClumps[clumpId];

    // ============================================================
    // ★ curve fetch (clump.curveId)
    // ============================================================
    uint curveId = cl.curveId;

    // volumeごとに曲線がまとまっているなら、範囲外を丸める
    // （設計が「曲線は全体共有でランダム」ならこのブロックは消してOK）
    if (gCurvesPerVolume != 0)
    {
        uint base = (uint)p.volumeSlot * gCurvesPerVolume;
        uint local = (curveId - base) % gCurvesPerVolume;
        curveId = base + local;
    }

    LeafGuideCurve c = gCurves[curveId];

    // ============================================================
    // ★ clump.s を中心に「粒子は少し前後に散る」
    //    p.s は個体差（-0.5..+0.5）として使う（Spawnでランダム入れておく）
    // ============================================================
    float s = frac(cl.s + (p.s - 0.5f) * max(gClumpLength01, 0.0f));

    float3 centerL = Bezier3(c.p0L, c.p1L, c.p2L, c.p3L, s);
    float3 tanL    = Bezier3Tangent(c.p0L, c.p1L, c.p2L, c.p3L, s);
    float3 tanWS   = SafeN(LocalToWorld(tanL, right, up, fwd));

    // curve cross-section basis
    float3 curveRightWS = SafeN(cross(up, tanWS));
    if (dot(curveRightWS, curveRightWS) < 1e-6) curveRightWS = right;

    float3 curveBinormWS = SafeN(cross(tanWS, curveRightWS));
    if (dot(curveBinormWS, curveBinormWS) < 1e-6) curveBinormWS = up;

    // ============================================================
    // ★ offset: clump中心 + 粒子個体差
    // ============================================================
    float lane   = cl.laneCenter   + p.lane;   // 粒子は微小な散らし
    float radial = cl.radialCenter + p.radial; // 上下（薄め推奨）

    float3 offsetWS = curveRightWS * lane + curveBinormWS * radial;

    float3 centerWS = vol.centerWS + LocalToWorld(centerL, right, up, fwd);
    float3 targetWS = centerWS + offsetWS;

    // ============================================================
    // steering: 横方向だけ（接線方向は追従しない）
    // ============================================================
    float3 toTarget = targetWS - p.posWS;
    float3 lateral = toTarget - tanWS * dot(toTarget, tanWS);
    // 高さは別制御したいならY追従を切る（地面追従を入れる場合おすすめ）
    // lateral.y = 0.0f;

    float3 steer = lateral * gFollowK;

    // ============================================================
    // flow: 接線方向へ流す（clump速度で統一）
    // ============================================================
    float speedAlong = max(vol.speed, 0.0f) * gWindAmplitude * max(cl.speedMul, 0.0f);
    float3 flow = tanWS * speedAlong;

    // flutter（弱め推奨：束感を壊すので）
    uint nseed = Hash_u32(id ^ cl.seed ^ (uint)(gTime * 60.0));
    float3 flutterDir = HashDir3(nseed);
    flutterDir.y *= 0.35;
    float3 flutter = flutterDir * (0.05f * vol.noiseScale);

    // player repel
    float3 repel = 0;
    float3 toP = p.posWS - gPlayerPosWS;
    float dP = length(toP);
    if (dP < gPlayerRepelRadius && dP > 1e-3)
    {
        float w = 1.0 - (dP / gPlayerRepelRadius);
        repel = (toP / dP) * (w * 6.0);
    }

    // integrate
    float3 accel = steer + flutter + repel;
    p.velWS += accel * dt;

    // flowへ寄せる（束感を出したいなら係数を上げる）
    p.velWS = lerp(p.velWS, flow, saturate(0.15 + 0.10 * vol.noiseScale));

    // damping (multiply)
    p.velWS *= gDamping;

    // clamp speed
    float sp = length(p.velWS);
    if (sp > gMaxSpeed)
        p.velWS *= (gMaxSpeed / max(sp, 1e-6));

    // pos
    p.posWS += p.velWS * dt;

    // kill if too far
    float3 dv = p.posWS - vol.centerWS;
    float dist = length(dv);
    float killR = vol.radius * max(gKillRadiusScale, 1.0);
    if (dist > killR)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint)-1);
        return;
    }

    // phase（ビルボード回転等に使う）
    p.phase += dt;

    // store + alive out
    gParticles[id] = p;
    gAliveOut.Append(id);
}
