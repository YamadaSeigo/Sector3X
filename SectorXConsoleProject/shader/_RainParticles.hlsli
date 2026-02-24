
#ifndef RAIN_PARTICLES_HLSLI
#define RAIN_PARTICLES_HLSLI

//深度のヒット判定の可視フラグ(※CPUのほうのフラグも外す RainParticlePool.h)
//#define DEBUG_RAIN_HIT_DEPTH

// RainParticles.hlsli
struct RainParticle
{
    float3 posWS;
    float life; // 0..1 or seconds

    float3 velWS;
    float addSize; // 加算サイズ

#ifdef DEBUG_RAIN_HIT_DEPTH
    uint debugHit;
#endif
};

#endif
