
// ========================= COMMON TYPES =========================
struct VSOut
{
    float4 pos : SV_Position;
    float3 n : NORMAL;
    float2 uv : TEXCOORD0;
};

struct ClusterLodRange
{
    uint offset;
    uint count;
};

// ========================= CONSTANTS =========================
cbuffer CSParams : register(b0)
{
    float4 FrustumPlanes[6]; // inward, normalized; dot(n, X) + d >= 0 is inside
    uint ClusterCount;
    uint _padCS0;
    uint _padCS1;
    uint _padCS2;

    // LOD selection params
    row_major float4x4 ViewProj; // same space as AABB (world)
    float2 ScreenSize; // pixels (e.g., 2560, 1440)
    float2 LodPxThreshold; // e.g., (T0, T1) for 3 levels; larger px -> higher detail
    uint LodLevels; // number of levels (1..N)
    uint _padCS3;
};

StructuredBuffer<uint> IndexPoolSRV : register(t0);
StructuredBuffer<uint2> ClusterIndexRange : register(t1);
StructuredBuffer<float3> ClusterAabbMin : register(t2);
StructuredBuffer<float3> ClusterAabbMax : register(t3);
StructuredBuffer<ClusterLodRange> ClusterLodRanges : register(t4);
StructuredBuffer<uint> LodBase : register(t5);
StructuredBuffer<uint> LodCount : register(t6);

RWByteAddressBuffer Counter : register(u0);
RWStructuredBuffer<uint> VisibleIndicesOut : register(u1);

// ========================= FRUSTUM TEST (center-extents + margin) =========================
static const float FRUSTUM_MARGIN_WORLD = 0.03f; // tune per scene (e.g., ~3cm)

bool AabbOutsidePlane_CE(float3 bmin, float3 bmax, float4 plane)
{
    float3 c = 0.5f * (bmin + bmax);
    float3 e = 0.5f * (bmax - bmin);
    float dist = dot(plane.xyz, c) + plane.w;
    float rad = dot(abs(plane.xyz), e);
    return dist < -(rad + FRUSTUM_MARGIN_WORLD);
}

bool AabbInFrustum(float3 bmin, float3 bmax)
{
    bool inside = true;
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        const bool outside = AabbOutsidePlane_CE(bmin, bmax, FrustumPlanes[i]);
        inside = inside && !outside;
    }
    return inside;
}

float ProjectedSizePx(float3 bmin, float3 bmax)
{
    // Approximate screen diameter of the AABB using two principal directions (x/z)
    float3 c = 0.5 * (bmin + bmax);
    float3 e = 0.5 * (bmax - bmin);
    float3 dx = float3(e.x, 0, 0);
    float3 dz = float3(0, 0, e.z);

    float4 hc = mul(ViewProj, float4(c, 1));
    float4 hx1 = mul(ViewProj, float4(c + dx, 1));
    float4 hx2 = mul(ViewProj, float4(c - dx, 1));
    float4 hz1 = mul(ViewProj, float4(c + dz, 1));
    float4 hz2 = mul(ViewProj, float4(c - dz, 1));

    float2 nc = hc.xy / max(hc.w, 1e-6);
    float2 nx1 = hx1.xy / max(hx1.w, 1e-6);
    float2 nx2 = hx2.xy / max(hx2.w, 1e-6);
    float2 nz1 = hz1.xy / max(hz1.w, 1e-6);
    float2 nz2 = hz2.xy / max(hz2.w, 1e-6);

    float2 dpx = max(abs(nx1 - nc), abs(nx2 - nc));
    dpx = max(dpx, max(abs(nz1 - nc), abs(nz2 - nc)));
    float2 px = dpx * 0.5 * ScreenSize; // NDC -> px
    return max(px.x, px.y) * 2.0; // diameter-ish
}

uint SelectLodPx(float sizePx)
{
    // Example for up to 3 levels using LodPxThreshold.xy
    // Larger on-screen size -> more detailed LOD (smaller index)
    if (LodLevels <= 1)
        return 0u;
    if (sizePx >= LodPxThreshold.x)
        return 0u; // LOD0
    if (LodLevels == 2)
        return 1u; // LOD1 only
    return (sizePx >= LodPxThreshold.y) ? 1u : 2u; // LOD1 or LOD2
}

// groupshared variable for base byte offset (must be global-scope)
groupshared uint g_baseBytes;

// ========================= GROUP BULK-RESERVATION KERNEL (LOD) =========================
[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint cid = groupID.x;
    if (cid >= ClusterCount)
    {
        return; // all threads take this path uniformly; no barrier reached
    }

    // Fetch cluster data (uniform across the group)
    float3 bmin = ClusterAabbMin[cid];
    float3 bmax = ClusterAabbMax[cid];

    // Frustum test
    bool visible = AabbInFrustum(bmin, bmax);

    // LOD selection
    uint lodIdx = 0u;
    if (visible)
    {
        float sizePx = ProjectedSizePx(bmin, bmax);
        lodIdx = SelectLodPx(sizePx);
        lodIdx = min(lodIdx, max(1u, LodCount[cid]) - 1u);
    }

    // Resolve range for the selected LOD (if any)
    uint2 r = (uint2) 0;
    if (visible)
    {
        uint base = LodBase[cid] + lodIdx;
        ClusterLodRange lr = ClusterLodRanges[base];
        r = uint2(lr.offset, lr.count);
    }

    uint triCount = visible ? (r.y / 3u) : 0u;

    // lane0 reserves a contiguous block once; unconditional barrier afterwards
    if (gtid.x == 0)
    {
        g_baseBytes = 0u;
        if (triCount > 0u)
        {
            Counter.InterlockedAdd(0 /*byteOffset*/, triCount * 12u /*bytes*/, g_baseBytes);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint base = g_baseBytes >> 2; // bytes -> uint elements

    // Each thread writes its stripe if there is any work
    for (uint ti = gtid.x; ti < triCount; ti += 64u)
    {
        uint i0 = IndexPoolSRV[r.x + ti * 3u + 0u];
        uint i1 = IndexPoolSRV[r.x + ti * 3u + 1u];
        uint i2 = IndexPoolSRV[r.x + ti * 3u + 2u];

        uint dst = base + ti * 3u;
        VisibleIndicesOut[dst + 0u] = i0;
        VisibleIndicesOut[dst + 1u] = i1;
        VisibleIndicesOut[dst + 2u] = i2;
    }
}