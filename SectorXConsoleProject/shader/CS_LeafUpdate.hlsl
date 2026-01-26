// CS_LeafUpdate_ClumpReadOnly.hlsl
// Firefly方式: AliveIn(SRV)+AliveCountRaw+Particles(UAV)+AliveOut(Append)+FreeList(Append)
// + Clumps(SRV) read-only

#include "_LeafParticles.hlsli"

// ----------------------------
// SRVs
// ----------------------------
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);
StructuredBuffer<uint> gAliveIn : register(t1);
ByteAddressBuffer gAliveCountRaw : register(t2);

Texture2D<float> gHeightMap : register(t3);
SamplerState gHeightSamp : register(s0);

StructuredBuffer<LeafGuideCurve> gCurves : register(t4);
StructuredBuffer<LeafClump> gClumps : register(t5);


Texture2D<float> gSceneDepth : register(t6);
SamplerState gDepthSamp : register(s1);


// ----------------------------
// UAVs
// ----------------------------
RWStructuredBuffer<LeafParticle> gParticles : register(u0);
AppendStructuredBuffer<uint> gAliveOut : register(u1);
AppendStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<uint> gVolumeCount : register(u3);

// ----------------------------
// CBs
// ----------------------------
cbuffer CBUpdate : register(b0)
{
    float gDt;
    float gTime;
    float2 _pad0;

    float3 gPlayerPosWS;
    float gPlayerRepelRadius;

    uint gClumpsPerVolume;
    uint gCurvesPerVolume;
    uint gTotalClumps; // activeVolumeCount*gClumpsPerVolume
    float gClumpLength01; // 例: 0.12
};

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

    float2 gSplatInvSize; // 1/width, 1/height (splat texture用)
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
    float3 gWindDir; // XZ平面想定
};

cbuffer CBParams : register(b3)
{
    float gKillRadiusScale; // 例: 1.5
    float gDamping; // 例: 0.96（multiply）
    float gFollowK; // 例: 6..14
    float gMaxSpeed; // 例: 6

    // ground params（Update側でも使うなら）
    float gGroundMinClear; // 例: 0.05（地面押し上げ最小クリア）
    float _padA, _padB, _padC;
};

cbuffer CBCamera : register(b4)
{
    row_major float4x4 gViewProj;
    float3 gCamRightWS;
    float gBaseSize; // billboard half-size 例: 0.05
    float3 gCamUpWS;
    float gCameraTime;

    float3 gCameraPosWS;
    float _padCam0;
    float2 gNearFar; // (near, far)  ※線形化に使う
    uint gDepthIsLinear01; // 1: すでに線形(0..1) / 0: D3Dのハードウェア深度
    float _padCam1;
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
float3 SafeN(float3 v)
{
    float l = length(v);
    return (l > 1e-6) ? (v / l) : float3(0, 1, 0);
}
float2 SafeN2(float2 v)
{
    float l = length(v);
    return (l > 1e-6) ? (v / l) : float2(1, 0);
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
    float u = 1 - t;
    float tt = t * t;
    float uu = u * u;
    return (uu * u) * p0 + 3 * (uu * t) * p1 + 3 * (u * tt) * p2 + (tt * t) * p3;
}
float3 Bezier3Tangent(float3 p0, float3 p1, float3 p2, float3 p3, float t)
{
    float u = 1 - t;
    return 3 * u * u * (p1 - p0) + 6 * u * t * (p2 - p1) + 3 * t * t * (p3 - p2);
}
float3 LocalToWorld(float3 local, float3 right, float3 up, float3 fwd)
{
    return right * local.x + up * local.y + fwd * local.z;
}

float LinearizeDepth01_D3D(float depth01, float nearZ, float farZ)
{
    // D3Dの通常深度(0..1)をview-space Zへ（正の距離）
    // z_ndc = depth01*2-1 の式もあるが、ここは一般的な「near/farで復元」パターンに寄せる
    float z = depth01;
    return (nearZ * farZ) / max(farZ - z * (farZ - nearZ), 1e-6f);
}

// ----------------------------
// Main
// ----------------------------
[numthreads(LEAF_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);
    uint i = tid.x;
    if (i >= aliveCount)
        return;

    uint id = gAliveIn[i];
    LeafParticle p = gParticles[id];
    LeafVolumeGPU vol = gVolumes[p.volumeSlot];

    float dt = gDt;

    // lifetime
    p.life -= dt;
    if (p.life <= 0.0f)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // wind basis
    float3 up = float3(0, 1, 0);
    float3 fwd = SafeN(float3(gWindDir.x, 0.0f, gWindDir.z));
    if (abs(dot(fwd, up)) > 0.98f)
        fwd = float3(0, 0, 1);

    float3 right3 = SafeN(cross(up, fwd));
    float3 binorm3 = SafeN(cross(fwd, right3));

    // clump read (SRV)
    uint clumpId = p.clumpId;
    if (clumpId >= gTotalClumps)
        clumpId = (uint) p.volumeSlot * gClumpsPerVolume + (clumpId % max(gClumpsPerVolume, 1u));

    LeafClump cl = gClumps[clumpId];

    // curve resolve
    uint curveId = cl.curveId;
    if (gCurvesPerVolume != 0)
    {
        uint base = (uint) p.volumeSlot * gCurvesPerVolume;
        uint local = (curveId - base) % gCurvesPerVolume;
        curveId = base + local;
    }
    LeafGuideCurve c = gCurves[curveId];

    // s: clump base + per-particle along spread
    float s = frac(cl.s + (p.s - 0.5f) * gClumpLength01);

    // evaluate curve center/tangent (local)
    float3 centerL = Bezier3(c.p0L, c.p1L, c.p2L, c.p3L, s);
    float3 tanL = Bezier3Tangent(c.p0L, c.p1L, c.p2L, c.p3L, s);

    float3 tanWS = SafeN(LocalToWorld(tanL, right3, up, fwd));

    // center in WS (Y offset comes from clump)
    float3 centerWS = vol.centerWS + LocalToWorld(centerL, right3, up, fwd);
    centerWS.y += cl.yOffset; // 地面追従はclump側で作った yOffset を適用

    float wobble = sin(gTime * 1.3f + cl.phase) * 3.0f; // ampは0.5～3mなど
    centerWS += right3 * wobble;

    centerWS.xz += cl.anchorXZ;

    // horizontal offset (clump center + particle variance)
    float lane = cl.laneCenter + p.lane;
    float radial = cl.radialCenter + p.radial;

    float2 rightXZ = SafeN2(right3.xz);
    float2 binormXZ = SafeN2(binorm3.xz);
    float2 offsetXZ = rightXZ * lane + binormXZ * radial;

    float2 targetXZ = centerWS.xz + offsetXZ;

    // ground push-up safety (sample at actual XZ)
    float groundY = SampleGroundY(targetXZ);

    // target position (Y is clump-driven; clamp to avoid underground)
    float targetY = max(centerWS.y, groundY + gGroundMinClear);
    float3 targetWS = float3(targetXZ.x, targetY, targetXZ.y);

    // steer only lateral (not along tangent)
    float3 toTarget = targetWS - p.posWS;
    float3 lateral = toTarget - tanWS * dot(toTarget, tanWS);
    float3 steer = lateral * gFollowK;

    // flow along tangent (use volume speed; cl.speedMul already baked into cl.s update)
    float speedAlong = max(vol.speed, 0.0f) * gWindAmplitude;
    float3 flow = tanWS * speedAlong;

    // flutter (weak)
    uint nseed = Hash_u32(id ^ cl.seed ^ (uint) (gTime * 60.0f));
    float3 flutterDir = HashDir3(nseed);
    flutterDir.y *= 0.25f;
    float3 flutter = flutterDir * (0.03f * vol.noiseScale);

    // player repel
    float3 repel = 0;
    float3 toP = p.posWS - gPlayerPosWS;
    float dP = length(toP);
    if (dP < gPlayerRepelRadius && dP > 1e-3f)
    {
        float w = 1.0f - (dP / gPlayerRepelRadius);
        repel = (toP / dP) * (w * 6.0f);
    }

    // integrate
    float3 accel = steer + flutter + repel;
    p.velWS += accel * dt;

    // blend toward flow for coherence
    p.velWS = lerp(p.velWS, flow, saturate(0.15f + 0.10f * vol.noiseScale));

    // damping
    p.velWS *= gDamping;

    // clamp speed
    float sp = length(p.velWS);
    if (sp > gMaxSpeed)
        p.velWS *= (gMaxSpeed / max(sp, 1e-6f));

    // position
    float3 prevPos = p.posWS;
    p.posWS += p.velWS * dt;

    // ---- screen-space depth collision ----
{
    // ワールド->クリップ->UV
        float4 clip = mul(gViewProj, float4(p.posWS, 1.0f));
        if (clip.w > 1e-6f)
        {
            float2 ndc = clip.xy / clip.w; // -1..1
            float2 uv;
            uv.x = ndc.x * 0.5f + 0.5f;
            uv.y = -ndc.y * 0.5f + 0.5f;

        // 画面内だけ判定（外は深度が当てにならないのでスキップ）
            if (all(uv >= 0.0f.xx) && all(uv <= 1.0f.xx))
            {
                float sceneD = gSceneDepth.SampleLevel(gDepthSamp, uv, 0);

            // 粒子の深度(0..1)  ※D3D想定：clip.z/clip.w → 0..1
                float particleD01 = saturate(clip.z / clip.w);

            // 比較は「線形同士」が安全
                float sceneZ = (gDepthIsLinear01 != 0) ? sceneD : LinearizeDepth01_D3D(sceneD, gNearFar.x, gNearFar.y);
                float partZ = (gDepthIsLinear01 != 0) ? particleD01 : LinearizeDepth01_D3D(particleD01, gNearFar.x, gNearFar.y);

            // 粒子が“シーンより奥”なら、壁の裏に行ってる＝貫通して見える可能性
            // biasは、チラつき防止（少し手前なら許す）
                float bias = 1.0f; // 3cm相当…は単位系次第。必要に応じて調整
                if (partZ > sceneZ - bias)
                {
                // 1) まずは位置を戻す（安全）
                    p.posWS = prevPos;

                // 2) 速度を減衰 + カメラ方向を基準に軽く反射（簡易）
                    float3 viewDir = SafeN(prevPos - gCameraPosWS); // カメラ→粒子
                    float3 n = -viewDir; // 画面上の“壁面法線”の近似（厳密ではないが見た目に効く）
                    p.velWS = reflect(p.velWS, n) * 0.35f;

                     // 壁に入る成分だけ除去版
                    //p.velWS -= n * min(0.0f, dot(p.velWS, n));
                    //p.velWS *= 0.4f;

#ifdef DEBUG_HIT_DEPTH
                    p.debugHit = 255;
#endif
                }
            }
        }
    }

#ifdef DEBUG_HIT_DEPTH
    p.debugHit = (p.debugHit > 0) ? (p.debugHit - 8) : 0; // 0.5秒くらいで消える感じ
#endif

    // final ground clamp at current pos (strong safety)
    float gyNow = SampleGroundY(p.posWS.xz);
    p.posWS.y = max(p.posWS.y, gyNow + gGroundMinClear);

    // kill if too far
    float3 dv = p.posWS - vol.centerWS;
    float dist = length(dv);
    float killR = vol.radius * max(gKillRadiusScale, 1.0f);
    if (dist > killR)
    {
        gFreeList.Append(id);
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    p.phase += dt;

    gParticles[id] = p;
    gAliveOut.Append(id);
}
