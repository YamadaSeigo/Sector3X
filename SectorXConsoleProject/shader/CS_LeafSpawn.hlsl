#include "_LeafParticles.hlsli"

// 入力（CPUが更新）
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);

Texture2D<float> gHeightMap : register(t1);

StructuredBuffer<LeafGuideCurve> gCurves : register(t2);

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
    uint gCurveCount; // FreeList枯渇対策（使わなくてもOK）
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

    // 目標数（float→uint）
    uint target = (uint) max(0.0f, v.targetCount);

    if (cur >= target)
        return;

    uint deficit = target - cur;
    uint spawnThisFrame = min(deficit, gMaxSpawnPerVolumePerFrame);

    if (lane >= spawnThisFrame)
        return;

    // FreeList から空きIDを取得（枯渇時は未定義になるので本番は対策推奨）
    uint id = gFreeList.Consume();

    // 初期化
    uint seed = v.seed ^ (slot * 9781u) ^ (id * 6271u) ^ Hash_u32((uint) (gTime * 60.0f));

    // 角度と半径でディスク一様サンプル
    float ang = Hash01(seed + 1u) * 6.2831853f; // 0..2π
    float r01 = sqrt(Hash01(seed + 2u)); // 0..1 を sqrt して面積一様
    float2 offset = float2(cos(ang), sin(ang)) * (r01 * v.radius);

    float2 xz = v.centerWS.xz + offset;

    float groundY = SampleGroundY(xz);
    float startY = /*v.centerWS.y +*/groundY; // 地面の高さを基準に
    startY += Hash01(seed + 4u) * 10.0f; // 地面直上 0..100cm

    float3 pos = float3(xz.x, startY, xz.y);
    pos -= gWindDir * (v.radius * 0.5f); // 風下に少しずらす

    LeafParticle p;
    p.posWS = pos;
    p.life = 1.0f;
    p.volumeSlot = slot;
    p.phase = Hash01(seed + 100u) * 6.2831853f;
    p.size = Hash01(seed + 200u) * gAddSize; // 0..1
    p.curveId = Hash_u32(seed + 300u) % max(gCurveCount, 1u);

    p.s = Hash01(seed + 400u);

    LeafGuideCurve c = gCurves[p.curveId];
    float3 tanL = Bezier3Tangent(c.p0L, c.p1L, c.p2L, c.p3L, p.s);

    // wind basis（Spawn側でも必要）
    float3 up3 = float3(0, 1, 0);
    float3 fwd = normalize(float3(gWindDir.x, 0, gWindDir.z));
    float3 right = normalize(cross(up3, fwd));
    float3 binorm = normalize(cross(fwd, right));

    float3 tanWS = normalize(LocalToWorld(tanL, right, up3, fwd));
    p.velWS = tanWS * v.speed;

    //矩形分布でランダム
    //p.lane = lerp(-gLaneMax, gLaneMax, Hash01(seed + 20u));
    //p.radial = lerp(-gRadMax, gRadMax, Hash01(seed + 21u));


    //中心が濃い「ガウスっぽい」分布
    float u1 = Hash01(seed + 20u);
    float u2 = Hash01(seed + 21u);
    float centerBiased = (u1 - u2); // -1..1 で中心寄り

    p.lane = centerBiased * gLaneMax;

    // radialはさらに薄く
    float v1 = Hash01(seed + 22u);
    float v2 = Hash01(seed + 23u);
    p.radial = (v1 - v2) * gRadMax;


    gParticles[id] = p;

    // Alive に積む（描画・Updateの入力）
    gAlive.Append(id);

    // slotの粒子数を増やす（競合するのでInterlockedAdd必須）
    InterlockedAdd(gVolumeCount[slot], 1);
}
