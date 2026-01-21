// _LeafParticles.hlsli

#ifndef LEAF_PARTICLES_HLSLI
#define LEAF_PARTICLES_HLSLI

struct LeafParticle
{
    float3 posWS;
    float life; // 0..1 か 秒

    float3 velWS;
    uint volumeSlot; // どの Volume に属するか

    float phase; // ひらひらアニメ用
    float size; // ビルボード半径
    float2 pad;
};

struct LeafVolumeGPU
{
    float3 centerWS;
    float radius;

    float3 color;
    float intensity;

    float targetCount; // LOD後の最終個数
    float speed; // 風に乗る基本速度
    float noiseScale; // 揺れのノイズスケール
    uint volumeSlot; // GPU側 slot index

    uint nearLightBudget; // 使わないなら 0 固定でOK
    uint seed;
    float burstT; // 0..1（たくさん舞う瞬間を作りたいなら）

    float pad0;
};

static const uint LEAF_THREAD_GROUP_SIZE = 256;

#endif // LEAF_PARTICLES_HLSLI
