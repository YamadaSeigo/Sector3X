// ================================================================
//  CS_TerrainClustered_CSMCombined.hlsl
//  - メインカメラ用 Visible_Main
//  - カスケードシャドウ用 Visible_Cascades (NUM_CASCADES)
//  - LOD: lodIdxMain / lodShadow[c] を分離
//  - Heightfield グリッドから直接インデックス生成（IndexPool 不使用）
//  - Full Perimeter Skirt（N/S/E/W 全周囲スカート）
// ================================================================
#include "_ShadowTypes.hlsli"

struct FrustumPlanes
{
    float4 planes[6]; // xyz = normal, w = d
};

// --------------------- cbuffer ---------------------
cbuffer CSParams : register(b4)
{
    // メインカメラ用フラスタム
    FrustumPlanes MainFrustum;

    // LOD 判定用（画面サイズベース）
    row_major float4x4 ViewProj;

    uint MaxVisibleIndices; // Visible_* の最大 index 数（uint 単位）
    uint LodLevels; // 期待値: 3~4（LOD0..LOD3）
    float2 ScreenSize; // px

    // メイン用 LOD しきい値 (px): x=0/1, y=1/2, z=2/3, w=3/4
    float4 LodPxThreshold_Main;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)
    uint gCellsX; // (= cellsX)
    uint gCellsZ; // (= cellsZ)
};

// --------------------- 入力バッファ ---------------------
// クラスタ AABB（ワールド）
StructuredBuffer<float3> ClusterAabbMin : register(t2);
StructuredBuffer<float3> ClusterAabbMax : register(t3);

// クラスタが参照する Heightfield 範囲（グローバルグリッド上の矩形）
// (startX, startZ)
StructuredBuffer<uint2> ClusterGridRect : register(t4);

// --------------------- UAV ---------------------
// u0: メインカメラ用カウンタ（byte 単位）
RWByteAddressBuffer Counter_Main : register(u0);
// u1: メインカメラ用 VisibleIndices
RWStructuredBuffer<uint> Visible_Main : register(u1);


// ================================================================
//  定数 / ヘルパ
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
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (AabbOutsidePlane_CE(bmin, bmax, fr.planes[i]))
        {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------
// LOD 判定ヘルパ（画面サイズ）
// ------------------------------------------------
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
    if (LodLevels <= 1)
        return 0;

    float t01 = LodPxThreshold_Main.x;
    float t12 = LodPxThreshold_Main.y;
    float t23 = LodPxThreshold_Main.z;
    float t34 = LodPxThreshold_Main.w;

    uint lod;
    if (sizePx >= t01)
        lod = 0;
    else if (sizePx >= t12)
        lod = 1;
    else if (sizePx >= t23)
        lod = 2;
    else if (sizePx >= t34)
        lod = 3;
    else
        lod = (LodLevels >= 5) ? 4u : 3u;

    return min(lod, LodLevels - 1);
}

// ------------------------------------------------
// グリッド Body 三角形生成
// ------------------------------------------------
void GetTriangleVertices_FromGrid(
    uint startX, uint startZ,
    uint cellsX, uint cellsZ,
    uint vertsX, // = gVertsX
    uint stride, // = 1<<lod
    uint triIndex, // [0..triCountBody)
    out uint v0, out uint v1, out uint v2)
{
    uint numQuadsX = cellsX / stride;
    uint numQuadsZ = cellsZ / stride;

    uint quadIdx = triIndex >> 1; // /2
    uint triInQuad = triIndex & 1u; // %2

    uint qx = quadIdx % numQuadsX;
    uint qz = quadIdx / numQuadsX;

    uint x0 = startX + qx * stride;
    uint z0 = startZ + qz * stride;
    uint x1 = x0 + stride;
    uint z1 = z0 + stride;

    uint v00 = z0 * vertsX + x0;
    uint v10 = z0 * vertsX + x1;
    uint v01 = z1 * vertsX + x0;
    uint v11 = z1 * vertsX + x1;

    if (triInQuad == 0)
    {
        v0 = v00;
        v1 = v10;
        v2 = v11; // (v00, v10, v11)
    }
    else
    {
        v0 = v00;
        v1 = v11;
        v2 = v01; // (v00, v11, v01)
    }
}

// ================================================================
//  groupshared
// ================================================================
groupshared uint g_baseBytesMain;

[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint cid = groupID.x;

    float3 bmin = ClusterAabbMin[cid];
    float3 bmax = ClusterAabbMax[cid];

    uint2 rect = ClusterGridRect[cid];
    uint startX = rect.x;
    uint startZ = rect.y;

    if (gCellsX == 0 || gCellsZ == 0)
        return;

    // --- フラスタム判定 ---
    bool visibleMain = AabbInFrustum(MainFrustum, bmin, bmax);

    // --- LOD 選択 ---
    float sizePx = ProjectedSizePx(bmin, bmax);

    static const float MIN_MAIN_PX = 5.0f;
    static const float MIN_SHADOW_PX = 10.0f;

    if (sizePx < MIN_MAIN_PX)
        visibleMain = false;

    uint lodIdxMain = 0;
    if (visibleMain)
    {
        lodIdxMain = SelectLodPx_Main(sizePx);
    }

    // ---------------- Main TriCount ----------------
    uint triBodyMain = 0;
    uint bytesMain = 0;
    uint strideMain = 1;

    if (visibleMain)
    {
        strideMain = (1u << lodIdxMain);
        uint numCellsX = gCellsX / strideMain;
        uint numCellsZ = gCellsZ / strideMain;

        if (numCellsX == 0 || numCellsZ == 0)
        {
            visibleMain = false;
        }
        else
        {
            // Body
            triBodyMain = numCellsX * numCellsZ * 2u;
            bytesMain = triBodyMain * 3u * 4u;
        }
    }

    // ---- カウンタ更新（スレッド0）----
    if (gtid.x == 0)
    {
        g_baseBytesMain = 0;

        if (visibleMain && bytesMain > 0u)
        {
            Counter_Main.InterlockedAdd(0, bytesMain, g_baseBytesMain);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint baseMainIdx = g_baseBytesMain >> 2;

    // =====================================================
    //  メイン: Body 三角形生成
    // =====================================================
    if (visibleMain && triBodyMain > 0u)
    {
        for (uint ti = gtid.x; ti < triBodyMain; ti += 64u)
        {
            uint v0, v1, v2;
            GetTriangleVertices_FromGrid(
                startX, startZ,
                gCellsX, gCellsZ,
                gVertsX,
                strideMain,
                ti,
                v0, v1, v2);

            uint dst = baseMainIdx + ti * 3u;

            Visible_Main[dst + 0] = v0;
            Visible_Main[dst + 1] = v1;
            Visible_Main[dst + 2] = v2;
        }
    }
}

