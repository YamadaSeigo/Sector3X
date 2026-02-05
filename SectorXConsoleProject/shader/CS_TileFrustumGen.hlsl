// CS_TileFrustumGen.hlsl
// Generates per-tile frustum side planes in VIEW space.

#include "_TileDeferred.hlsli"

#ifndef FRUSTUM_BLOCK_SIZE
#define FRUSTUM_BLOCK_SIZE 8
#endif

struct Plane
{
    float3 n; // normal (points inward)
    float d; // plane constant
};

struct TileFrustum
{
    Plane left;
    Plane right;
    Plane top;
    Plane bottom;
};

// Output: one frustum per tile
RWStructuredBuffer<TileFrustum> gTileFrustums : register(u0);

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

// Convert pixel coords to NDC.
// DX-style NDC: x,y in [-1,1]. y is typically flipped vs texture space.
float2 PixelToNDC(float2 pix)
{
    float2 ndc;
    ndc.x = (pix.x / (float) gScreenWidth) * 2.0f - 1.0f;
    ndc.y = 1.0f - (pix.y / (float) gScreenHeight) * 2.0f;
    return ndc;
}

// Unproject a point on the near plane to view space.
// We use z = 0 as "near" in NDC for DX convention in clip space after projection.
// Depending on your projection convention, you might want z=0 or z=1.
// If it looks wrong, this is the first knob to adjust.
float3 NDCToViewNear(float2 ndc)
{
    float4 clip = float4(ndc, 0.0f, 1.0f);
    float4 v = mul(gInvProj, clip); // row_major + col-vector convention
    v.xyz /= max(v.w, 1e-6f);
    return v.xyz;
}

// Make a plane that goes through the origin and contains two rays a and b.
// The plane normal direction depends on cross order.
// We'll fix orientation so that it points inward (dot(n, testRay) >= 0).
Plane MakeSidePlane(float3 a, float3 b, float3 insideTestRay)
{
    Plane p;
    float3 n = normalize(cross(a, b));
    // Ensure inside is positive
    if (dot(n, insideTestRay) < 0.0f)
        n = -n;
    p.n = n;
    p.d = 0.0f; // passes through origin in view space
    return p;
}

[numthreads(FRUSTUM_BLOCK_SIZE, FRUSTUM_BLOCK_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // We run 1 thread per tile (so dispatch gTilesX x gTilesY with numthreads 1x1 is simpler),
    // but using 8x8 is fine if you map only dtid.x/y as tile coords and ignore extras.
    // For simplicity, treat each thread as one tile here:
    uint tx = dtid.x;
    uint ty = dtid.y;
    if (tx >= gTilesX || ty >= gTilesY)
        return;

    // Tile pixel bounds
    float x0 = (float) (tx * TILE_SIZE);
    float y0 = (float) (ty * TILE_SIZE);
    float x1 = min(x0 + (float) TILE_SIZE, (float) gScreenWidth);
    float y1 = min(y0 + (float) TILE_SIZE, (float) gScreenHeight);

    // 4 corners in NDC (use corners, or center-of-pixel if you prefer)
    float2 ndc00 = PixelToNDC(float2(x0, y0)); // top-left
    float2 ndc10 = PixelToNDC(float2(x1, y0)); // top-right
    float2 ndc01 = PixelToNDC(float2(x0, y1)); // bottom-left
    float2 ndc11 = PixelToNDC(float2(x1, y1)); // bottom-right

    // Rays in view space (from origin). We can use points on near plane directly as rays.
    float3 r00 = NDCToViewNear(ndc00);
    float3 r10 = NDCToViewNear(ndc10);
    float3 r01 = NDCToViewNear(ndc01);
    float3 r11 = NDCToViewNear(ndc11);

    // A ray roughly through tile center for "inside" orientation
    float2 ndcC = PixelToNDC(float2(0.5f * (x0 + x1), 0.5f * (y0 + y1)));
    float3 rC = NDCToViewNear(ndcC);

    // Build side planes. The chosen cross orders define the plane; we then flip if needed.
    // Left plane contains r00 & r01
    // Right plane contains r11 & r10
    // Top plane contains r10 & r00
    // Bottom plane contains r01 & r11
    TileFrustum f;
    f.left = MakeSidePlane(r01, r00, rC);
    f.right = MakeSidePlane(r10, r11, rC);
    f.top = MakeSidePlane(r00, r10, rC);
    f.bottom = MakeSidePlane(r11, r01, rC);

    uint tileIndex = ty * gTilesX + tx;
    gTileFrustums[tileIndex] = f;
}
