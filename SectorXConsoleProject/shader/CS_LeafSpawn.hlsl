#include "_LeafParticles.hlsli"

// 入力（CPUが更新）
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);

Texture2D<float> gHeightMap : register(t1);

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
    uint gMaxParticles; // FreeList枯渇対策（使わなくてもOK）
    float gAddSize;
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

    // 侵入バースト中は少し多めに spawn（2～4倍まで）
    if (v.burstT > 0.0f)
    {
        spawnThisFrame = min(deficit, gMaxSpawnPerVolumePerFrame * 3);
    }

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
    startY += Hash01(seed + 4u) * 1.0f; // 地面直上 0..100cm

    float3 pos = float3(xz.x, startY, xz.y);

    // --- 上向き: 弱く、ランダムに
    float up = 0.25f + 0.35f * Hash01(seed + 11u); // 0.25..0.60

    // --- 水平: かなり弱く
    float2 h = float2(Hash01(seed + 10u) * 2 - 1,
                  Hash01(seed + 12u) * 2 - 1);
    h = normalize(h) * (0.05f + 0.15f * Hash01(seed + 13u)); // 0.05..0.20

    float3 vel = float3(h.x, up, h.y) * v.speed;

    LeafParticle p;
    p.posWS = pos;
    p.life = 1.0f;
    p.velWS = vel;
    p.volumeSlot = slot;
    p.phase = Hash01(seed + 100u) * 6.2831853f;
    p.size = Hash01(seed + 200u) * gAddSize; // 0..1
    p.pad = float2(0.0f, 0.0f);

    gParticles[id] = p;

    // Alive に積む（描画・Updateの入力）
    gAlive.Append(id);

    // slotの粒子数を増やす（競合するのでInterlockedAdd必須）
    InterlockedAdd(gVolumeCount[slot], 1);
}
