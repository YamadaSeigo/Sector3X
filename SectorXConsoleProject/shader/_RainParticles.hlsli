
#ifndef RAIN_PARTICLES_HLSLI
#define RAIN_PARTICLES_HLSLI

// RainParticles.hlsli
struct RainParticle
{
    float3 posWS;
    float life; // 0..1 or seconds

    float3 velWS;
    float addSize; // â¡éZÉTÉCÉY
};

#endif
