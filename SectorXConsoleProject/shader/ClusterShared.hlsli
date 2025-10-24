cbuffer ViewCB : register(b0)
{
    row_major float4x4 uViewProj;
    float2 uViewportWH; // (W, H)
    float uProjScale; // viewportH / (2*tan(fovY/2))
    float _pad0;
};

struct ClusterInfo
{
    uint indexStart;
    uint indexCount;
    uint bucketId;
    uint flags;
    float3 aabbMin;
    float geomError;
    float3 aabbMax;
    uint _pad1;
};

StructuredBuffer<ClusterInfo> gClusters : register(t0);

// 最大バケツ数（LOD×ステッチ×マテリアル等）
#ifndef MAX_BUCKETS
#define MAX_BUCKETS 32
#endif

// Append するペア：x=bucket, y=clusterIndex
struct PairBU
{
    uint bucket;
    uint index;
};

cbuffer LodCB : register(b1)
{
    float gTauIn;
    float gTauOut;
    float gHystBias;
    float _pad2;
};

cbuffer OcclCB : register(b2)
{
    uint gUseHiZ; // 0/1
    uint _pad3_0;
    uint _pad3_1;
    uint _pad3_2;
};

Texture2D<float> gHiZ : register(t10); // mipmapped
SamplerState gPointClamp : register(s0);

// AABB -> NDC rect + min w / min depth (ZeroToOne, LH)
struct NdcRectWithDepth
{
    float xmin, ymin, xmax, ymax;
    float wmin;
    float zmin; // min(clip.z / clip.w) in [0,1]
    uint valid; // use uint to avoid any compiler quirk around bool
};

// NOTE: function parameter is just float4x4 (no row_major here)
static NdcRectWithDepth ProjectAabb_ToNdc_MinWZ(float4x4 WVP, float3 bmin, float3 bmax)
{
    float3 c[8] =
    {
        float3(bmin.x, bmin.y, bmin.z), float3(bmax.x, bmin.y, bmin.z),
        float3(bmin.x, bmax.y, bmin.z), float3(bmax.x, bmax.y, bmin.z),
        float3(bmin.x, bmin.y, bmax.z), float3(bmax.x, bmin.y, bmax.z),
        float3(bmin.x, bmax.y, bmax.z), float3(bmax.x, bmax.y, bmax.z)
    };

    float xmin = 1e9, ymin = 1e9, xmax = -1e9, ymax = -1e9;
    float wmin = 1e9, zmin = 1e9;
    uint any = 0;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float4 clip = mul(WVP, float4(c[i], 1.0));
        if (clip.w <= 0.0)
            continue; // behind eye

        float2 ndc = clip.xy / clip.w;
        float z = clip.z / clip.w; // ZeroToOne: [0,1]

        xmin = min(xmin, ndc.x);
        ymin = min(ymin, ndc.y);
        xmax = max(xmax, ndc.x);
        ymax = max(ymax, ndc.y);
        wmin = min(wmin, clip.w);
        zmin = min(zmin, z);

        any |= (abs(ndc.x) <= 1.5 && abs(ndc.y) <= 1.5) ? 1u : 0u;
    }

    NdcRectWithDepth r;
    r.xmin = xmin;
    r.ymin = ymin;
    r.xmax = xmax;
    r.ymax = ymax;
    r.wmin = wmin;
    r.zmin = zmin;
    r.valid = (any != 0u) &&
              ((xmin <= 1.2 && xmax >= -1.2 && ymin <= 1.2 && ymax >= -1.2) ? 1u : 0u);
    return r;
}

// Hi-Z occlusion test.
// Returns true if the whole rect is covered by nearer depth.
static bool HiZ_Occluded(float2 ndcMin, float2 ndcMax, float occludeeZmin, float2 viewportWH)
{
    // NDC [-1,1] -> pixel
    float2 pxMin = (ndcMin * 0.5f + 0.5f) * viewportWH;
    float2 pxMax = (ndcMax * 0.5f + 0.5f) * viewportWH;
    float2 size = max(pxMax - pxMin, 0.0);

    if (max(size.x, size.y) < 4.0)
        return false;

    float longest = max(size.x, size.y);
    int mip = max((int) floor(log2(longest)) - 1, 0);

    // Query texture size (use uints)
    uint texW, texH;
    gHiZ.GetDimensions(texW, texH);
    float2 texSize = float2((float) texW, (float) texH) / exp2((float) mip);

    // UVs are simply pixel / viewport
    float2 uvMin = pxMin / viewportWH;
    float2 uvMax = pxMax / viewportWH;

    float2 uvs[6] =
    {
        lerp(uvMin, uvMax, float2(0.5, 0.5)),
        uvMin, uvMax,
        float2(uvMin.x, uvMax.y),
        float2(uvMax.x, uvMin.y),
        lerp(uvMin, uvMax, float2(0.0, 0.5))
    };

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        float maxNearest = gHiZ.SampleLevel(gPointClamp, uvs[i], mip);
        if (maxNearest >= occludeeZmin)
            return false; // some point could be visible
    }
    return true;
}
