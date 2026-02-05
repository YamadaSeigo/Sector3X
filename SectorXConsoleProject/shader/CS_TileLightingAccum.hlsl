// CS_TiledLighting_Accum.hlsl
// Apply per-tile culled point lights and write lighting accumulation texture.

#include "_TileDeferred.hlsli"

struct PointLight
{
    float3 positionWS;
    float range;

    float3 color;
    float intensity;

    float invRadius;
    uint flag;
};

// Light buffers (if you do two-buffers style)
StructuredBuffer<PointLight> gNormalLights : register(t0);
StructuredBuffer<PointLight> gFireflyLights : register(t1);

// Tile lists
StructuredBuffer<uint> gTileLightCount : register(t2);
StructuredBuffer<uint> gTileLightIndices : register(t3);

// GBuffer
Texture2D<float4> gAlbedoTex : register(t4);
Texture2D<float4> gNormalTex : register(t5);
Texture2D<float> gDepthTex : register(t6);

// Output
RWTexture2D<float4> gLightAccum : register(u0);

SamplerState gPointSampler : register(s0); // for Load you don't need sampler; kept for flexibility

cbuffer TiledCB : register(b0)
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

// Pixel -> NDC
float2 PixelToNDC(float2 pix)
{
    float2 ndc;
    ndc.x = (pix.x / (float) gScreenWidth) * 2.0f - 1.0f;
    ndc.y = 1.0f - (pix.y / (float) gScreenHeight) * 2.0f;
    return ndc;
}

float3 ReconstructWorldPos(uint2 pix, float depth01)
{
    float2 ndc = PixelToNDC((float2) pix + 0.5f);

    // DX style: clip.z is often depth01 in [0,1], but some pipelines expect [-1,1].
    // If world pos looks wrong, switch to depth01*2-1.
    float4 clip = float4(ndc, depth01, 1.0f);
    // float4 clip = float4(ndc, depth01 * 2.0f - 1.0f, 1.0f);

    float4 wp = mul(gInvViewProj, clip); // row_major + col-vector
    wp.xyz /= max(wp.w, 1e-6f);
    return wp.xyz;
}

float3 DecodeNormal(float4 nrm)
{
    // Assuming normal stored in xyz in [-1,1]
    float3 N = nrm.xyz;
    return normalize(N);
}

float3 ShadePointLight(float3 wp, float3 N, float3 albedo, PointLight pl)
{
    float3 toL = pl.positionWS - wp;
    float distSq = dot(toL, toL);
    float range = pl.range;
    float rangeSq = range * range;
    if (range <= 0.0f || distSq >= rangeSq)
        return 0.0f;

    distSq = max(distSq, 1e-6f);
    float invDist = rsqrt(distSq);
    float dist = distSq * invDist;
    float3 L = toL * invDist;

    // cheap smooth falloff (similar to what you used)
    float t = saturate(dist * pl.invRadius);
    float att = 1.0f - t;
    att *= att;

    float ndl = saturate(dot(N, L));

    float3 radiance = pl.color * pl.intensity * att;
    return radiance * (albedo * ndl);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint tx = gid.x;
    uint ty = gid.y;

    uint2 pix = uint2(tx * TILE_SIZE + gtid.x, ty * TILE_SIZE + gtid.y);
    if (pix.x >= gScreenWidth || pix.y >= gScreenHeight)
        return;

    uint tileIndex = ty * gTilesX + tx;

    float4 albedo4 = gAlbedoTex.Load(int3(pix, 0));
    float3 albedo = albedo4.rgb;

    float4 nrm4 = gNormalTex.Load(int3(pix, 0));
    float3 N = DecodeNormal(nrm4);

    float depth01 = gDepthTex.Load(int3(pix, 0));
    float3 wp = ReconstructWorldPos(pix, depth01);

    uint count = gTileLightCount[tileIndex];
    uint base = tileIndex * MAX_LIGHTS_PER_TILE;

    float4 sum = 0.0f;

    [loop]
    for (uint i = 0; i < count; ++i)
    {
        uint enc = gTileLightIndices[base + i];
        uint type = DecodeType(enc);
        uint idx = DecodeIndex(enc);

        PointLight pl;
        if (type == 0)
            pl = gNormalLights[idx];
        else
            pl = gFireflyLights[idx];

        sum.rgb += ShadePointLight(wp, N, albedo, pl);
    }

    gLightAccum[pix] = sum;
}
