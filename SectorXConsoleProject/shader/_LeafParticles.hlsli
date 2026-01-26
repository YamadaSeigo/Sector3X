// _LeafParticles.hlsli

#ifndef LEAF_PARTICLES_HLSLI
#define LEAF_PARTICLES_HLSLI

static const uint LEAF_THREAD_GROUP_SIZE = 256;

//深度のヒット判定の可視フラグ(※CPUのほうのフラグも外す LeafParticlePool.h)
//#define DEBUG_HIT_DEPTH

struct LeafParticle
{
    float3 posWS;
    float life; // seconds

    float3 velWS;
    uint volumeSlot;

    float phase;
    float size;

    uint clumpId; // which guide curve
    float s; // 0..1 progress on curve

    float lane; // offset along curve-right (meters)
    float radial; // offset along curve-binormal (meters)
    
    float life0; // 初期寿命(sec)
    float3 tint; // 葉っぱ固有色

#ifdef DEBUG_HIT_DEPTH
    uint debugHit;
#endif
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

// Guide curve is stored in LOCAL space (volume-centered basis space):
// x = right, y = up, z = forward (wind direction)
struct LeafGuideCurve
{
    float3 p0L;
    float3 p1L;
    float3 p2L;
    float3 p3L;
    float lengthApprox; // used for ds/dt normalization
};

struct LeafClump
{
    uint curveId;
    float s;

    float laneCenter;
    float radialCenter;

    float speedMul; // 0.8..1.2
    float phase;
    uint seed;
    float yOffset;
    float yVel;

    float2 anchorXZ; // clumpの水平アンカー（ボリューム中心からのオフセット）
    float2 anchorVelXZ;
};


#endif // LEAF_PARTICLES_HLSLI
