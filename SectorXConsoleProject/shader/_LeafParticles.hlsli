// _LeafParticles.hlsli

#ifndef LEAF_PARTICLES_HLSLI
#define LEAF_PARTICLES_HLSLI

static const uint LEAF_THREAD_GROUP_SIZE = 256;

//深度のヒット判定の可視フラグ(※CPUのほうのフラグも外す LeafParticlePool.h)
//#define DEBUG_LEAF_HIT_DEPTH

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
    
    uint normalOct; // 擬似法線（WS）を oct32 で保持

#ifdef DEBUG_LEAF_HIT_DEPTH
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

float2 OctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-8);
    float2 e = n.xy;
    if (n.z < 0.0)
    {
        e = (1.0 - abs(e.yx)) * (e.xy >= 0.0 ? 1.0 : -1.0);
    }
    return e; // -1..1
}

float3 OctDecode(float2 e)
{
    float3 n = float3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0 ? -t : t);
    return normalize(n);
}

uint PackSnorm2x16(float2 v) // v: -1..1
{
    int2 i = int2(round(saturate(v * 0.5 + 0.5) * 65535.0)) * 2 - 65535; // snorm-ish
    uint2 u = uint2(i & 0xFFFF);
    return (u.x) | (u.y << 16);
}

float2 UnpackSnorm2x16(uint p)
{
    int2 i;
    i.x = (int) (p & 0xFFFF);
    i.y = (int) ((p >> 16) & 0xFFFF);
    // sign extend
    i.x = (i.x << 16) >> 16;
    i.y = (i.y << 16) >> 16;
    return float2(i) / 32767.0; // approx -1..1
}

uint PackNormalOct(float3 n)
{
    float2 e = OctEncode(normalize(n));
    return PackSnorm2x16(e);
}

float3 UnpackNormalOct(uint p)
{
    float2 e = UnpackSnorm2x16(p);
    return OctDecode(e);
}

#endif // LEAF_PARTICLES_HLSLI
