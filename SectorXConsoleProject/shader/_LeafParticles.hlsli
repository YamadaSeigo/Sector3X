// _LeafParticles.hlsli

#ifndef LEAF_PARTICLES_HLSLI
#define LEAF_PARTICLES_HLSLI

struct LeafParticle
{
    float3 posWS;
    float life; // seconds

    float3 velWS;
    uint volumeSlot;

    float phase;
    float size;

    uint curveId; // which guide curve
    float s; // 0..1 progress on curve

    float lane; // offset along curve-right (meters)
    float radial; // offset along curve-binormal (meters)
};

struct LeafVolumeGPU
{
    float3 centerWS;
    float radius;

    float3 color;
    float intensity;

    float targetCount;
    float speed; // base speed along wind/curve
    float noiseScale;
    uint volumeSlot;

    uint seed;
    uint pad0;
    float pad1;
    float pad2;
};

static const uint LEAF_THREAD_GROUP_SIZE = 256;

#endif // LEAF_PARTICLES_HLSLI
