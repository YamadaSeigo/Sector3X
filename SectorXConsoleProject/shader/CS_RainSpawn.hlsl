#include "_RainParticles.hlsli"

// aliveCount は raw buffer
ByteAddressBuffer gAliveCountRaw : register(t0);

// 出力
RWStructuredBuffer<RainParticle> gParticles : register(u0);
AppendStructuredBuffer<uint> gAlive : register(u1);
ConsumeStructuredBuffer<uint> gFreeList : register(u2);

cbuffer CBSpawn : register(b0)
{
    float3 gCamPos;
    float gTime;

    uint gMaxSpawnPerFrame; // 例：32
    uint gMaxParticles; // FreeList枯渇対策（使わなくてもOK）
    float gHeightOffset; // カメラからの高さオフセット
    float gAddSize;

    float gSpawnRadius; // スポーン位置の半径
    float gLife; // 初期寿命
    float _pad0, _pad1; // 16B 境界揃え
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
float Hash01(uint x)
{
    return (Hash_u32(x) & 0x00FFFFFFu) / 16777216.0f;
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // 1D dispatch を「(volumeIndex * gMaxSpawnPerVolumePerFrame) + local」
    uint global = tid.x;

    uint aliveCount = gAliveCountRaw.Load(0);
    if(aliveCount + global >= gMaxParticles)
        return;

    // FreeList から空きIDを取得（枯渇時は未定義になるので本番は対策推奨）
    uint id = gFreeList.Consume();

    // 初期化
    uint seed = (id * 6271u) ^ Hash_u32((uint) (gTime * 60.0f));

    // 角度と半径でディスク一様サンプル
    float ang = Hash01(seed + 1u) * 6.2831853f; // 0..2π
    float r01 = sqrt(Hash01(seed + 2u)); // 0..1 を sqrt して面積一様
    float2 offset = float2(cos(ang), sin(ang)) * (r01 * gSpawnRadius);

    float2 xz = gCamPos.xz + offset;

    float startY = gCamPos.y + gHeightOffset; // カメラからの高さオフセット
    startY += Hash01(seed + 4u) * 1.0f; // 0..1mのランダムな高さを足す

    float3 pos = float3(xz.x, startY, xz.y);

    RainParticle p;
    p.posWS = pos;
    p.life = gLife;
    p.velWS = float3(0, 0, 0);
    p.addSize = Hash01(seed + 100u) * gAddSize; // 0..1

    gParticles[id] = p;

    // Alive に積む（描画・Updateの入力）
    gAlive.Append(id);
}
