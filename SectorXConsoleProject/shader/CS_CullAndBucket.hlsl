#include "ClusterShared.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    uint num, stride;
    gClusters.GetDimensions(num, stride);
    if (i >= num)
        return;

    ClusterInfo c = gClusters[i];

    // 1) フラスタム/NDC
    NdcRectWithDepth r = ProjectAabb_ToNdc_MinWZ(uViewProj, c.aabbMin, c.aabbMax);
    if (!r.valid)
        return;

    // 2) LOD（画面誤差 σ）
    float depthW = max(r.wmin, 1e-3);
    float sigma = (c.geomError * uProjScale) / depthW; // 簡易
    // （雛形では bucketId をオフラインで決め済みとし、そのまま使用）
    uint bucket = c.bucketId;

    // 3) Hi-Z（必要なら）
    if (gUseHiZ != 0)
    {
        // occludee の “最も手前” の深度値（0..1）
        float zmin = saturate(r.zmin);
        if (HiZ_Occluded(float2(r.xmin, r.ymin), float2(r.xmax, r.ymax),
                         zmin, uViewportWH))
            return; // 完全に隠れている
    }

    // 4) 可視圧縮
    //if (bucket < MAX_BUCKETS)
        //gVisibleBuckets[bucket].Append(i);
}
