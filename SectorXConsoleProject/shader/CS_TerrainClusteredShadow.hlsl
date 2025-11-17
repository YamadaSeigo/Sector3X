// ================================================================
//  CS_TerrainClustered_CSMCombined.hlsl
//  - メインカメラ用 Visible_Main
//  - カスケードシャドウ用 Visible_Cascades (4カスケード)
//  - LOD: lodIdxMain / lodIdxShadow を分離
// ================================================================
#include "_ShadowTypes.hlsli"

struct ClusterLodRange
{
    uint offset;
    uint count;
};

struct FrustumPlanes
{
    float4 planes[6]; // xyz = normal, w = d
};

//struct CSParamsShadowCombined
//{
//    float MainFrustum[6][4];
//    float CascadeFrustum[MaxShadowCascades][6][4];
//    float ViewProj[16];
//    UINT ClusterCount;
//    UINT LodLevels;
//    float ScreenSize[2];
//    float LodPxThreshold_Main[2];
//    float LodPxThreshold_Shadow[2];
//};

// --------------------- cbuffer ---------------------
cbuffer CSParams : register(b4)
{
    // メインカメラ用フラスタム
    FrustumPlanes MainFrustum;

    // カスケード用フラスタム (0..3)
    FrustumPlanes CascadeFrustum[NUM_CASCADES];

    // LOD 判定用（画面サイズベース、通常はメインカメラの ViewProj）
    row_major float4x4 ViewProj;

    uint MaxVisibleIndices;
    uint LodLevels;

    float2 ScreenSize;

    // メイン用 LOD しきい値 (px)
    float2 LodPxThreshold_Main; // x: LOD0/1, y: LOD1/2

    // シャドウ用 LOD しきい値 (px)
    float2 LodPxThreshold_Shadow; // x: LOD0/1, y: LOD1/2
};

// --------------------- 入力バッファ ---------------------
StructuredBuffer<uint> IndexPoolSRV : register(t0);
StructuredBuffer<uint2> ClusterIndexRange : register(t1); // 使っていなければ無視してOK
StructuredBuffer<float3> ClusterAabbMin : register(t2);
StructuredBuffer<float3> ClusterAabbMax : register(t3);
StructuredBuffer<ClusterLodRange> ClusterLodRanges : register(t4);
StructuredBuffer<uint> LodBase : register(t5);
StructuredBuffer<uint> LodCount : register(t6);

// --------------------- UAV ---------------------
// u0: メインカメラ用カウンタ（byte 単位）
RWByteAddressBuffer Counter_Main : register(u0);
// u1: メインカメラ用 VisibleIndices
RWStructuredBuffer<uint> Visible_Main : register(u1);

// u2: カスケード用カウンタ [C0, C1, C2, C3] (各4byte)
RWByteAddressBuffer CascadeCounters : register(u2);
// u3: カスケード全体の VisibleIndices (4カスケード連結)
RWStructuredBuffer<uint> Visible_Cascades : register(u3);

// ================================================================
//  フラスタム判定ヘルパ
// ================================================================
static const float FRUSTUM_MARGIN_WORLD = 0.03f;

bool AabbOutsidePlane_CE(float3 bmin, float3 bmax, float4 plane)
{
    float3 c = 0.5f * (bmin + bmax);
    float3 e = 0.5f * (bmax - bmin);
    float dist = dot(plane.xyz, c) + plane.w;
    float rad = dot(abs(plane.xyz), e);
    return dist < -(rad + FRUSTUM_MARGIN_WORLD);
}

bool AabbInFrustum(in FrustumPlanes fr, float3 bmin, float3 bmax)
{
    bool inside = true;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (AabbOutsidePlane_CE(bmin, bmax, fr.planes[i]))
        {
            inside = false;
        }
    }

    return inside;
}

// ================================================================
//  LOD 判定ヘルパ（画面サイズ）
// ================================================================
float ProjectedSizePx(float3 bmin, float3 bmax)
{
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
    float2 px = dpx * (0.5 * ScreenSize);

    return max(px.x, px.y) * 2.0; // 直径
}

// メイン用 LOD 選択
uint SelectLodPx_Main(float sizePx)
{
    uint lod = 0;

    if (LodLevels > 1)
    {
        if (LodLevels == 2)
        {
            lod = (sizePx >= LodPxThreshold_Main.x) ? 0u : 1u;
        }
        else
        {
            lod = (sizePx >= LodPxThreshold_Main.x) ? 0u :
                  (sizePx >= LodPxThreshold_Main.y) ? 1u : 2u;
        }
    }
    return min(lod, LodLevels - 1);
}

// シャドウ用 LOD 選択（たとえば閾値を少し小さめにして荒くする）
uint SelectLodPx_Shadow(float sizePx)
{
    uint lod = 0;

    if (LodLevels > 1)
    {
        if (LodLevels == 2)
        {
            lod = (sizePx >= LodPxThreshold_Shadow.x) ? 0u : 1u;
        }
        else
        {
            lod = (sizePx >= LodPxThreshold_Shadow.x) ? 0u :
                  (sizePx >= LodPxThreshold_Shadow.y) ? 1u : 2u;
        }
    }
    return min(lod, LodLevels - 1);
}

// ================================================================
//  groupshared
// ================================================================
groupshared uint g_baseBytesMain;
groupshared uint g_baseBytesCascade[NUM_CASCADES];

[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint cid = groupID.x;

    // 1) Dispatch を (clusterCount,1,1) できているなら、cid >= ClusterCount チェック自体を消してOK
    //    どうしても残したい場合は return ではなくフラグにする（後述）

    float3 bmin = ClusterAabbMin[cid];
    float3 bmax = ClusterAabbMax[cid];

    // --- フラスタム判定 ---
    bool visibleMain = AabbInFrustum(MainFrustum, bmin, bmax);

    bool visibleCascade[NUM_CASCADES];
    [unroll]
    for (int c0 = 0; c0 < NUM_CASCADES; ++c0)
        visibleCascade[c0] = AabbInFrustum(CascadeFrustum[c0], bmin, bmax);

    bool anyCascade = false;
    [unroll]
    for (int c1 = 0; c1 < NUM_CASCADES; ++c1)
        anyCascade = anyCascade || visibleCascade[c1];

    // --- LOD 選択 ---
    float sizePx = ProjectedSizePx(bmin, bmax);

    uint lodIdxMain = SelectLodPx_Main(sizePx);
    uint lodIdxShadow = SelectLodPx_Shadow(sizePx);

    // triCount 計算
    uint triCountMain = 0;
    uint triCountShadow = 0;
    uint indexOffsetMain = 0;
    uint indexOffsetShadow = 0;

    if (visibleMain)
    {
        // メイン用
        uint baseMainLod = LodBase[cid] + lodIdxMain;
        ClusterLodRange lrM = ClusterLodRanges[baseMainLod];
        indexOffsetMain = lrM.offset;
        uint indexCountMain = lrM.count;
        triCountMain = indexCountMain / 3u;
    }

    if (anyCascade)
    {
        // シャドウ用（全カスケード共通の LOD）
        uint baseShadowLod = LodBase[cid] + lodIdxShadow;
        ClusterLodRange lrS = ClusterLodRanges[baseShadowLod];
        indexOffsetShadow = lrS.offset;
        uint indexCountShadow = lrS.count;
        triCountShadow = indexCountShadow / 3u;
    }

    uint bytesMain = triCountMain * 3u * 4u;
    uint bytesShadow = triCountShadow * 3u * 4u;

    // スレッド0がカウンタを更新
    if (gtid.x == 0)
    {
        g_baseBytesMain = 0;
        [unroll]
        for (int c = 0; c < NUM_CASCADES; ++c)
            g_baseBytesCascade[c] = 0;

        if (visibleMain && bytesMain > 0u)
        {
            Counter_Main.InterlockedAdd(0, bytesMain, g_baseBytesMain);
        }

        if (anyCascade && bytesShadow > 0u)
        {
            [unroll]
            for (int c = 0; c < NUM_CASCADES; ++c)
            {
                if (visibleCascade[c])
                {
                    uint offsetBytes = c * 4u;
                    CascadeCounters.InterlockedAdd(offsetBytes, bytesShadow, g_baseBytesCascade[c]);
                }
            }
        }
    }

    // ここは必ず全スレッドが通るので OK
    GroupMemoryBarrierWithGroupSync();

    uint baseMainIdx = g_baseBytesMain >> 2;
    uint baseCasIdx[NUM_CASCADES];
    [unroll]
    for (int c = 0; c < NUM_CASCADES; ++c)
        baseCasIdx[c] = g_baseBytesCascade[c] >> 2;

    // 書き込み
    if (visibleMain && triCountMain > 0u)
    {
        for (uint ti = gtid.x; ti < triCountMain; ti += 64u)
        {
            uint i0 = IndexPoolSRV[indexOffsetMain + ti * 3u + 0u];
            uint i1 = IndexPoolSRV[indexOffsetMain + ti * 3u + 1u];
            uint i2 = IndexPoolSRV[indexOffsetMain + ti * 3u + 2u];

            uint dst = baseMainIdx + ti * 3u;
            Visible_Main[dst + 0u] = i0;
            Visible_Main[dst + 1u] = i1;
            Visible_Main[dst + 2u] = i2;
        }
    }

    if (anyCascade && triCountShadow > 0u)
    {
        for (uint ti = gtid.x; ti < triCountShadow; ti += 64u)
        {
            uint i0 = IndexPoolSRV[indexOffsetShadow + ti * 3u + 0u];
            uint i1 = IndexPoolSRV[indexOffsetShadow + ti * 3u + 1u];
            uint i2 = IndexPoolSRV[indexOffsetShadow + ti * 3u + 2u];

            [unroll]
            for (int c = 0; c < NUM_CASCADES; ++c)
            {
                if (!visibleCascade[c])
                    continue;

                uint dst = MaxVisibleIndices * c + baseCasIdx[c] + ti * 3u;
                Visible_Cascades[dst + 0u] = i0;
                Visible_Cascades[dst + 1u] = i1;
                Visible_Cascades[dst + 2u] = i2;
            }
        }
    }

    // ここから先で return するのは OK（もうバリアが出てこないなら）
}
