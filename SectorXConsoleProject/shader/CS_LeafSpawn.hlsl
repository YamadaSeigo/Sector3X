#include "_LeafParticles.hlsli"

// 入力（CPUが更新）
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);

Texture2D<float> gHeightMap : register(t1);

StructuredBuffer<LeafGuideCurve> gCurves : register(t2);

StructuredBuffer<LeafClump> gClumps : register(t3);


SamplerState gHeightSamp : register(s0);

// 出力
RWStructuredBuffer<LeafParticle> gParticles : register(u0);
AppendStructuredBuffer<uint> gAlive : register(u1);
ConsumeStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<uint> gVolumeCount : register(u3);

cbuffer CBSpawn : register(b0)
{
    float3 gPlayerPosWS;
    float gTime;

    uint gActiveVolumeCount;
    uint gMaxSpawnPerVolumePerFrame; // 例：32
    uint gClumpsPerVolume;
    float gAddSize;

    float gLaneMin;
    float gLaneMax;
    float gRadMin;
    float gRadMax;
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

    float2 gSplatInvSize; // 1/width, 1/height (splat texture用)

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

float SampleGroundY(float2 xz)
{
    float2 terrainSize = gClusterXZ * float2(gDimX, gDimZ);
    float2 uv = saturate((xz - gOriginXZ) / terrainSize);
    float h = gHeightMap.SampleLevel(gHeightSamp, uv, 0);
    return h * heightScale + offsetY;
}

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
float Hash01(uint x)
{
    return (Hash_u32(x) & 0x00FFFFFFu) / 16777216.0f;
}
float RandRange(uint s, float a, float b)
{
    return lerp(a, b, Hash01(s));
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

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
     // 1D dispatch を「(volumeIndex * gMaxSpawnPerVolumePerFrame) + local」
    uint global = tid.x;
    uint volIdx = global / gMaxSpawnPerVolumePerFrame;
    uint lane = global - volIdx * gMaxSpawnPerVolumePerFrame;

    if (volIdx >= gActiveVolumeCount)
        return;

    LeafVolumeGPU v = gVolumes[volIdx];
    uint slot = v.volumeSlot;

    // 現在数（slot単位）
    uint cur = gVolumeCount[slot];

    // 目標数
    uint target = (uint) max(0.0f, v.targetCount);
    if (cur >= target)
        return;

    uint deficit = target - cur;
    uint spawnThisFrame = min(deficit, gMaxSpawnPerVolumePerFrame);
    if (lane >= spawnThisFrame)
        return;

    // FreeList から空きID
    uint id = gFreeList.Consume();

    // seed
    uint seed = v.seed ^ (slot * 9781u) ^ (id * 6271u) ^ Hash_u32((uint) (gTime * 60.0f));

    // ----------------------------
    // Spawn position
    // ----------------------------
    // ディスク一様サンプル（ボリューム半径内）
    float ang = Hash01(seed + 1u) * 6.2831853f;
    float r01 = sqrt(Hash01(seed + 2u));
    float2 offset = float2(cos(ang), sin(ang)) * (r01 * v.radius);

    float2 xz = v.centerWS.xz + offset;

    float groundY = SampleGroundY(xz);
    float startY = groundY + Hash01(seed + 4u) * 6.0f; // 地面直上 0..1m

    LeafParticle p;
    p.posWS = float3(xz.x, startY, xz.y);
    p.velWS = float3(0, 0, 0);

    // ----------------------------
    // Lifetime
    // ----------------------------
    // 例：3~8秒。好みで調整
    float lifeSec = lerp(3.0f, 8.0f, Hash01(seed + 10u));
    p.life = lifeSec;

    p.volumeSlot = slot;

    // phase：ビルボード回転に使うなら 0..2π
    p.phase = Hash01(seed + 100u) * 6.2831853f;

    // サイズ
    p.size = Hash01(seed + 200u) * gAddSize;

    // ----------------------------
    // clump assignment (global clump id)
    // ----------------------------
    uint clLocal = (gClumpsPerVolume > 0) ? (Hash_u32(seed + 300u) % gClumpsPerVolume) : 0u;
    p.clumpId = volIdx * gClumpsPerVolume + clLocal;

    // ----------------------------
    // clump内の前後散らし（0..1）
    // Update側で cl.s を中心に (p.s-0.5)*clumpLength01 で使う想定
    // ----------------------------
    p.s = Hash01(seed + 400u);

    // ----------------------------
    // 個体差オフセット（clump中心に足されるので小さめ推奨）
    // ----------------------------
    p.lane = RandRange(seed + 20u, gLaneMin, gLaneMax);
    p.radial = RandRange(seed + 21u, gRadMin, gRadMax);

    float t0 = Hash01(seed);
    float t1 = Hash01(seed ^ 0xA53C9E1Bu);

    // 例：緑->黄->茶 を軽くブレさせる
    float3 baseA = float3(0.25, 0.75, 0.20); // green
    float3 baseB = float3(0.70, 0.75, 0.20); // yellow-green
    //float3 baseC = float3(0.55, 0.35, 0.15); // brown

    //float3 tint = (t0 < 0.6) ? lerp(baseA, baseB, t0 / 0.6)
    //                     : lerp(baseB, baseC, (t0 - 0.6) / 0.4);

    float3 tint = lerp(baseA, baseB, t0);

    // 明るさも少しブレ
    tint *= lerp(0.85, 1.15, t1);

    p.tint = tint;
    p.life0 = p.life; // 生成直後の寿命を保存

#ifdef DEBUG_HIT_DEPTH
    p.debugHit = 0;
#endif

    gParticles[id] = p;

    // Aliveへ
    gAlive.Append(id);

    // slotの粒子数増加
    InterlockedAdd(gVolumeCount[slot], 1);
}
