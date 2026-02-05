// CS_TileCull_TwoBuffers.hlsl
// - 16x16 threads = 1 tile
// - compute tile min/max viewZ from depth
// - cull two light buffers (Normal + Firefly) into one per-tile index list
// - store encoded indices: (type<<31) | index

#include "_TileDeferred.hlsli"


// -----------------------------
// Your PointLight struct
// -----------------------------
struct PointLight
{
    float3 positionWS;
    float range; // 16B

    float3 color;
    float intensity; // 16B

    float invRadius;
    uint flag;

    // NOTE: In StructuredBuffer, HLSL packs to 16-byte multiples.
    // On CPU side, ensure stride matches (typically 48 bytes if you add padding to 16B).
    // If you get stride mismatch warnings, pad the CPU struct to 48 bytes.
};

// -----------------------------
// Tile frustum planes (view space)
// -----------------------------
struct Plane
{
    float3 n;
    float d;
};
struct TileFrustum
{
    Plane left, right, top, bottom;
};

// Inputs
StructuredBuffer<PointLight> gNormalLights : register(t0);
StructuredBuffer<PointLight> gFireflyLights : register(t1);
StructuredBuffer<TileFrustum> gTileFrustums : register(t2);
Texture2D<float> gDepth : register(t3); // depth 0..1 SRV

// Outputs
RWStructuredBuffer<uint> gTileLightCount : register(u0); // tiles
RWStructuredBuffer<uint> gTileLightIndices : register(u1); // tiles * MAX_LIGHTS_PER_TILE

cbuffer TileCB : register(b0)
{
    uint gScreenWidth;
    uint gScreenHeight;
    uint gTilesX;
    uint gTilesY;
};

cbuffer CameraCB : register(b1)
{
    row_major float4x4 gView; // world -> view (adjust mul order if column-vector convention)
    row_major float4x4 gInvProj; // inverse projection (view space)
    row_major float4x4 gInvViewProj; // clip->world
    float3 gCamPosWS;
    uint _pad0;
};


cbuffer LightCountCB : register(b2)
{
    uint gNormalLightCount;
    uint gFireflyLightCount;
    uint _pad0_count;
    uint _pad1_count;
}

// -----------------------------
// Helpers
// -----------------------------
float2 PixelToNDC(float2 pix)
{
    float2 ndc;
    ndc.x = (pix.x / (float) gScreenWidth) * 2.0f - 1.0f;
    ndc.y = 1.0f - (pix.y / (float) gScreenHeight) * 2.0f;
    return ndc;
}

// Reconstruct view-space position from (ndc.xy, depth)
// IMPORTANT: depends on your projection convention.
// If min/maxZ looks wrong, switch between Option A/B.
float3 ReconstructViewPos(float2 ndc, float depth01)
{
    // Option A (DX clip.z in [0,1]):
    float4 clip = float4(ndc, depth01, 1.0f);

    // Option B (if your invProj expects clip.z in [-1,1]):
    // float4 clip = float4(ndc, depth01 * 2.0f - 1.0f, 1.0f);

    float4 v = mul(gInvProj, clip); // row_major + col-vector convention
    v.xyz /= max(v.w, 1e-6f);
    return v.xyz;
}

bool SphereIntersectsPlane(float3 cVS, float r, Plane p)
{
    return (dot(p.n, cVS) + p.d) >= -r;
}

bool SphereIntersectsTileFrustum(float3 cVS, float r, TileFrustum f)
{
    if (!SphereIntersectsPlane(cVS, r, f.left))
        return false;
    if (!SphereIntersectsPlane(cVS, r, f.right))
        return false;
    if (!SphereIntersectsPlane(cVS, r, f.top))
        return false;
    if (!SphereIntersectsPlane(cVS, r, f.bottom))
        return false;
    return true;
}

// -----------------------------
// groupshared
// -----------------------------
groupshared float gsMinZ;
groupshared float gsMaxZ;
groupshared uint gsCount;
groupshared uint gsEncoded[MAX_LIGHTS_PER_TILE];

groupshared float gsReduseMin[256];
groupshared float gsReduseMax[256];

void ReduceMinMaxZ(float z, uint tid)
{
    gsReduseMin[tid] = z;
    gsReduseMax[tid] = z;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128; stride >= 1; stride >>= 1)
    {
        if (tid < stride)
        {
            gsReduseMin[tid] = min(gsReduseMin[tid], gsReduseMin[tid + stride]);
            gsReduseMax[tid] = max(gsReduseMax[tid], gsReduseMax[tid + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
        if (stride == 1)
            break;
    }

    if (tid == 0)
    {
        gsMinZ = gsReduseMin[0];
        gsMaxZ = gsReduseMax[0];
    }
    GroupMemoryBarrierWithGroupSync();
}

void TryAppend(uint encoded)
{
    uint slot;
    InterlockedAdd(gsCount, 1, slot);
    if (slot < MAX_LIGHTS_PER_TILE)
        gsEncoded[slot] = encoded;
}

// Z range reject using tile min/max viewZ.
// Assumes viewZ increases with distance (camera forward is +Z).
// If your view forward is -Z, swap the comparisons accordingly.
bool PassZRange(float cz, float r)
{
    if ((cz - r) > gsMaxZ)
        return false;
    if ((cz + r) < gsMinZ)
        return false;
    return true;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint tx = gid.x;
    uint ty = gid.y;
    if (tx >= gTilesX || ty >= gTilesY)
        return;

    uint tileIndex = ty * gTilesX + tx;
    TileFrustum fr = gTileFrustums[tileIndex];

    // 1) depth min/max (viewZ)
    uint2 pix = uint2(tx * TILE_SIZE + gtid.x, ty * TILE_SIZE + gtid.y);
    pix.x = min(pix.x, gScreenWidth - 1);
    pix.y = min(pix.y, gScreenHeight - 1);

    float depth01 = saturate(gDepth.Load(int3(pix, 0)));

    float2 ndc = PixelToNDC((float2) pix + 0.5f);
    float3 vpos = ReconstructViewPos(ndc, depth01);

    float z = vpos.z;

    uint tid = gtid.y * TILE_SIZE + gtid.x;
    ReduceMinMaxZ(z, tid);

    // 2) cull lights in parallel, pack into group list
    if (tid == 0)
        gsCount = 0;
    GroupMemoryBarrierWithGroupSync();

    const uint THREADS = TILE_SIZE * TILE_SIZE;

    // ---- Normal lights ----
    for (uint li = tid; li < gNormalLightCount; li += THREADS)
    {
        PointLight pl = gNormalLights[li];
        float r = pl.range;
        if (r <= 0.0f)
            continue;

        float3 cVS = mul(gView, float4(pl.positionWS, 1.0f)).xyz;

        // Optional behind-camera reject (assuming +Z forward):
        if (cVS.z + r <= 0.0f)
            continue;

        if (!PassZRange(cVS.z, r))
            continue;
        if (!SphereIntersectsTileFrustum(cVS, r, fr))
            continue;

        TryAppend(EncodeLightIndex(LIGHT_TYPE_NORMAL, li));
    }

    // ---- Firefly lights ----
    for (uint fi = tid; fi < gFireflyLightCount; fi += THREADS)
    {
        PointLight pl = gFireflyLights[fi];
        float r = pl.range;
        if (r <= 0.0f)
            continue;

        float3 cVS = mul(gView, float4(pl.positionWS, 1.0f)).xyz;

        if (cVS.z + r <= 0.0f)
            continue;

        if (!PassZRange(cVS.z, r))
            continue;
        if (!SphereIntersectsTileFrustum(cVS, r, fr))
            continue;

        TryAppend(EncodeLightIndex(LIGHT_TYPE_FIREFLY, fi));
    }

    GroupMemoryBarrierWithGroupSync();

    // 3) write out once per tile
    if (tid == 0)
    {
        uint count = min(gsCount, (uint) MAX_LIGHTS_PER_TILE);
        gTileLightCount[tileIndex] = count;

        uint base = tileIndex * MAX_LIGHTS_PER_TILE;
        [loop]
        for (uint i = 0; i < count; ++i)
            gTileLightIndices[base + i] = gsEncoded[i];
    }
}
