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

    // カスケード用フラスタム (0..NUM_CASCADES-1)
    FrustumPlanes CascadeFrustum[NUM_CASCADES];

    // LOD 判定用（画面サイズベース）
    row_major float4x4 ViewProj;

    uint MaxVisibleIndices; // Visible_* の最大 index 数（uint 単位）
    uint LodLevels; // 期待値: 3~4（LOD0..LOD3）
    float2 ScreenSize; // px

    // メイン用 LOD しきい値 (px): x=0/1, y=1/2, z=2/3, w=3/4
    float4 LodPxThreshold_Main;

    // シャドウ用 LOD しきい値 (px)
    float4 LodPxThreshold_Shadow;
};

// --------------------- 入力バッファ ---------------------
// クラスタ AABB（ワールド）
StructuredBuffer<float3> ClusterAabbMin : register(t2);
StructuredBuffer<float3> ClusterAabbMax : register(t3);

// クラスタが参照する Heightfield 範囲（グローバルグリッド上の矩形）
// (startX, startZ, cellsX, cellsZ)
StructuredBuffer<uint4> ClusterGridRect : register(t4);

// --------------------- UAV ---------------------
// u0: メインカメラ用カウンタ（byte 単位）
RWByteAddressBuffer Counter_Main : register(u0);
// u1: メインカメラ用 VisibleIndices
RWStructuredBuffer<uint> Visible_Main : register(u1);

// u2: カスケード用カウンタ [C0, C1, C2, C3,...] (各4byte)
RWByteAddressBuffer CascadeCounters : register(u2);
// u3: カスケード全体の VisibleIndices (カスケード連結)
RWStructuredBuffer<uint> Visible_Cascades : register(u3);

// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)

    uint2 padding; // 未使用

    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height
};

// ================================================================
//  定数 / ヘルパ
// ================================================================
static const float FRUSTUM_MARGIN_WORLD = 0.03f;
static const uint SKIRT_FLAG = 0x80000000u;

uint PackIndex(uint vertexIndex, bool isSkirtBottom)
{
    // vertexIndex は 0..(gVertsX*gVertsZ-1) を想定（2^31 未満）
    return vertexIndex | (isSkirtBottom ? SKIRT_FLAG : 0u);
}

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

// シャドウ用基準 LOD
uint SelectLodPx_ShadowBase(float sizePx)
{
    if (LodLevels <= 1)
        return 0;

    float t01 = LodPxThreshold_Shadow.x;
    float t12 = LodPxThreshold_Shadow.y;
    float t23 = LodPxThreshold_Shadow.z;
    float t34 = LodPxThreshold_Shadow.w;

    uint lod;
    if (sizePx >= t01)
        lod = 0;
    else if (sizePx >= t12)
        lod = 1;
    else if (sizePx >= t23 && LodLevels >= 3)
        lod = 2;
    else if (sizePx >= t34 && LodLevels >= 4)
        lod = 3;
    else
        lod = (LodLevels >= 3) ? 4u : 3u;

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
groupshared uint g_baseBytesCascade[NUM_CASCADES];

[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint cid = groupID.x;

    float3 bmin = ClusterAabbMin[cid];
    float3 bmax = ClusterAabbMax[cid];

    uint4 rect = ClusterGridRect[cid];
    uint startX = rect.x;
    uint startZ = rect.y;
    uint cellsX = rect.z;
    uint cellsZ = rect.w;

    if (cellsX == 0 || cellsZ == 0)
        return;

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

    static const float MIN_MAIN_PX = 5.0f;
    static const float MIN_SHADOW_PX = 10.0f;

    if (sizePx < MIN_MAIN_PX)
        visibleMain = false;

    if (sizePx < MIN_SHADOW_PX)
    {
        [unroll]
        for (int c = 0; c < NUM_CASCADES; ++c)
            visibleCascade[c] = false;
        anyCascade = false;
    }

    uint lodIdxMain = 0;
    if (visibleMain)
    {
        lodIdxMain = SelectLodPx_Main(sizePx);
    }

    // カスケード用 LOD
    uint lodShadowBase = 0;
    uint lodShadow[NUM_CASCADES];
    if (anyCascade)
    {
        lodShadowBase = SelectLodPx_ShadowBase(sizePx);

        [unroll]
        for (int c = 0; c < NUM_CASCADES; ++c)
        {
            uint l = lodShadowBase + (uint) c; // 遠いカスケードほど LOD 粗く
            if (l >= LodLevels)
                l = LodLevels - 1u;
            lodShadow[c] = l;
        }
    }

    // ---------------- Main TriCount ----------------
    uint triBodyMain = 0;
    uint triSkirtMain = 0;
    uint bytesMain = 0;
    uint strideMain = 1;

    if (visibleMain)
    {
        strideMain = (1u << lodIdxMain);
        uint numCellsX = cellsX / strideMain;
        uint numCellsZ = cellsZ / strideMain;

        if (numCellsX == 0 || numCellsZ == 0)
        {
            visibleMain = false;
        }
        else
        {
            // Body
            triBodyMain = numCellsX * numCellsZ * 2u;

            // Skirt: 全周囲, 各辺のセグメント数 = numCellsX or numCellsZ
            uint numSegX = numCellsX;
            uint numSegZ = numCellsZ;
            uint segTotal = 2u * (numSegX + numSegZ); // N/S/E/W の総セグメント
            triSkirtMain = segTotal * 2u; // 各セグメント 2 三角

            uint triTotalMain = triBodyMain + triSkirtMain;
            bytesMain = triTotalMain * 3u * 4u;
        }
    }

    // ---------------- Shadow TriCount ----------------
    uint triBodyShadow[NUM_CASCADES];
    uint triSkirtShadow[NUM_CASCADES];
    uint bytesShadow[NUM_CASCADES];

    [unroll]
    for (int c = 0; c < NUM_CASCADES; ++c)
    {
        triBodyShadow[c] = 0;
        triSkirtShadow[c] = 0;
        bytesShadow[c] = 0;

        if (!visibleCascade[c])
            continue;

        uint scStride = (1u << lodShadow[c]);
        uint numCellsX = cellsX / scStride;
        uint numCellsZ = cellsZ / scStride;

        if (numCellsX == 0 || numCellsZ == 0)
        {
            visibleCascade[c] = false;
            continue;
        }

        triBodyShadow[c] = numCellsX * numCellsZ * 2u;

        uint numSegX = numCellsX;
        uint numSegZ = numCellsZ;
        uint segTotal = 2u * (numSegX + numSegZ);
        triSkirtShadow[c] = segTotal * 2u;

        uint triTotal = triBodyShadow[c] + triSkirtShadow[c];
        bytesShadow[c] = triTotal * 3u * 4u;
    }

    // ---- カウンタ更新（スレッド0）----
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

        [unroll]
        for (int ci = 0; ci < NUM_CASCADES; ++ci)
        {
            if (visibleCascade[ci] && bytesShadow[ci] > 0u)
            {
                uint offsetBytes = ci * 4u;
                CascadeCounters.InterlockedAdd(offsetBytes, bytesShadow[ci], g_baseBytesCascade[ci]);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint baseMainIdx = g_baseBytesMain >> 2;
    uint baseCasIdx[NUM_CASCADES];
    [unroll]
    for (int ci = 0; ci < NUM_CASCADES; ++ci)
        baseCasIdx[ci] = g_baseBytesCascade[ci] >> 2;

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
                cellsX, cellsZ,
                gVertsX,
                strideMain,
                ti,
                v0, v1, v2);

            uint dst = baseMainIdx + ti * 3u;

            Visible_Main[dst + 0] = PackIndex(v0, false);
            Visible_Main[dst + 1] = PackIndex(v1, false);
            Visible_Main[dst + 2] = PackIndex(v2, false);
        }
    }

    // =====================================================
    //  メイン: Skirt 三角形生成（Full Perimeter）
    // =====================================================
    if (visibleMain && triSkirtMain > 0u)
    {
        uint numCellsX = cellsX / strideMain;
        uint numCellsZ = cellsZ / strideMain;

        uint numSegX = numCellsX;
        uint numSegZ = numCellsZ;

        // Body の後ろからスカートを書き始める
        uint skirtTriBase = triBodyMain;

        // Edge tri offset 累積
        uint triOffset = 0;

        // ---- North (z = startZ) ----
        {
            uint edgeTriCount = numSegX * 2u; // 1セグメント2三角
            for (uint t = gtid.x; t < edgeTriCount; t += 64u)
            {
                uint seg = t >> 1;
                uint triInSeg = t & 1u;

                uint x0 = startX + seg * strideMain;
                uint x1 = x0 + strideMain;
                uint z0 = startZ;

                uint vTop0 = z0 * gVertsX + x0;
                uint vTop1 = z0 * gVertsX + x1;

                uint dstTri = skirtTriBase + triOffset + t;
                uint dst = baseMainIdx + dstTri * 3u;

                uint top0 = PackIndex(vTop0, false);
                uint top1 = PackIndex(vTop1, false);
                uint bot0 = PackIndex(vTop0, true);
                uint bot1 = PackIndex(vTop1, true);

                if (triInSeg == 0)
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot1;
                    Visible_Main[dst + 2] = top1;
                }
                else
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot0;
                    Visible_Main[dst + 2] = bot1;
                }
            }
            triOffset += edgeTriCount;
        }

        // ---- South (z = startZ + cellsZ) ----
        {
            uint edgeTriCount = numSegX * 2u;
            uint z1 = startZ + cellsZ;

            for (uint t = gtid.x; t < edgeTriCount; t += 64u)
            {
                uint seg = t >> 1;
                uint triInSeg = t & 1u;

                uint x0 = startX + seg * strideMain;
                uint x1 = x0 + strideMain;

                uint vTop0 = z1 * gVertsX + x0;
                uint vTop1 = z1 * gVertsX + x1;

                uint dstTri = skirtTriBase + triOffset + t;
                uint dst = baseMainIdx + dstTri * 3u;

                uint top0 = PackIndex(vTop0, false);
                uint top1 = PackIndex(vTop1, false);
                uint bot0 = PackIndex(vTop0, true);
                uint bot1 = PackIndex(vTop1, true);

                if (triInSeg == 0)
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = top1;
                    Visible_Main[dst + 2] = bot1;
                }
                else
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot1;
                    Visible_Main[dst + 2] = bot0;
                }
            }
            triOffset += edgeTriCount;
        }

        // ---- West (x = startX) ----
        {
            uint edgeTriCount = numSegZ * 2u;
            uint x0_const = startX;

            for (uint t = gtid.x; t < edgeTriCount; t += 64u)
            {
                uint seg = t >> 1;
                uint triInSeg = t & 1u;

                uint z0 = startZ + seg * strideMain;
                uint z1 = z0 + strideMain;

                uint vTop0 = z0 * gVertsX + x0_const;
                uint vTop1 = z1 * gVertsX + x0_const;

                uint dstTri = skirtTriBase + triOffset + t;
                uint dst = baseMainIdx + dstTri * 3u;

                uint top0 = PackIndex(vTop0, false);
                uint top1 = PackIndex(vTop1, false);
                uint bot0 = PackIndex(vTop0, true);
                uint bot1 = PackIndex(vTop1, true);

                if (triInSeg == 0)
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = top1;
                    Visible_Main[dst + 2] = bot1;
                }
                else
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot1;
                    Visible_Main[dst + 2] = bot0;
                }
            }
            triOffset += edgeTriCount;
        }

        // ---- East (x = startX + cellsX) ----
        {
            uint edgeTriCount = numSegZ * 2u;
            uint x1_const = startX + cellsX;

            for (uint t = gtid.x; t < edgeTriCount; t += 64u)
            {
                uint seg = t >> 1;
                uint triInSeg = t & 1u;

                uint z0 = startZ + seg * strideMain;
                uint z1 = z0 + strideMain;

                uint vTop0 = z0 * gVertsX + x1_const;
                uint vTop1 = z1 * gVertsX + x1_const;

                uint dstTri = skirtTriBase + triOffset + t;
                uint dst = baseMainIdx + dstTri * 3u;

                uint top0 = PackIndex(vTop0, false);
                uint top1 = PackIndex(vTop1, false);
                uint bot0 = PackIndex(vTop0, true);
                uint bot1 = PackIndex(vTop1, true);

                if (triInSeg == 0)
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot1;
                    Visible_Main[dst + 2] = top1;
                }
                else
                {
                    Visible_Main[dst + 0] = top0;
                    Visible_Main[dst + 1] = bot0;
                    Visible_Main[dst + 2] = bot1;
                }
            }
            triOffset += edgeTriCount;
        }
    }

    // =====================================================
    //  カスケード側（Body + Skirt）（パターンは Main と同じ）
    // =====================================================
    [unroll]
    for (int cn = 0; cn < NUM_CASCADES; ++cn)
    {
        if (!visibleCascade[cn])
            continue;

        uint triBodyC = triBodyShadow[cn];
        uint triSkirtC = triSkirtShadow[cn];
        if (triBodyC == 0 && triSkirtC == 0)
            continue;

        uint strideC = (1u << lodShadow[cn]);
        uint numCellsX = cellsX / strideC;
        uint numCellsZ = cellsZ / strideC;
        uint numSegX = numCellsX;
        uint numSegZ = numCellsZ;

        uint baseIdxCascade = baseCasIdx[cn];

        // ---- Body ----
        if (triBodyC > 0u)
        {
            for (uint ti = gtid.x; ti < triBodyC; ti += 64u)
            {
                uint v0, v1, v2;
                GetTriangleVertices_FromGrid(
                    startX, startZ,
                    cellsX, cellsZ,
                    gVertsX,
                    strideC,
                    ti,
                    v0, v1, v2);

                uint dst = MaxVisibleIndices * cn + baseIdxCascade + ti * 3u;

                Visible_Cascades[dst + 0] = PackIndex(v0, false);
                Visible_Cascades[dst + 1] = PackIndex(v1, false);
                Visible_Cascades[dst + 2] = PackIndex(v2, false);
            }
        }

        // ---- Skirt ----
        if (triSkirtC > 0u)
        {
            uint skirtTriBase = triBodyC;
            uint triOffset = 0;

            // North
            {
                uint edgeTriCount = numSegX * 2u;
                uint z0 = startZ;

                for (uint t = gtid.x; t < edgeTriCount; t += 64u)
                {
                    uint seg = t >> 1;
                    uint triInSeg = t & 1u;

                    uint x0 = startX + seg * strideC;
                    uint x1 = x0 + strideC;

                    uint vTop0 = z0 * gVertsX + x0;
                    uint vTop1 = z0 * gVertsX + x1;

                    uint dstTri = skirtTriBase + triOffset + t;
                    uint dst = MaxVisibleIndices * cn + baseIdxCascade + dstTri * 3u;

                    uint top0 = PackIndex(vTop0, false);
                    uint top1 = PackIndex(vTop1, false);
                    uint bot0 = PackIndex(vTop0, true);
                    uint bot1 = PackIndex(vTop1, true);

                    if (triInSeg == 0)
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = top1;
                        Visible_Cascades[dst + 2] = bot1;
                    }
                    else
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot1;
                        Visible_Cascades[dst + 2] = bot0;
                    }
                }
                triOffset += edgeTriCount;
            }

            // South
            {
                uint edgeTriCount = numSegX * 2u;
                uint z1 = startZ + cellsZ;

                for (uint t = gtid.x; t < edgeTriCount; t += 64u)
                {
                    uint seg = t >> 1;
                    uint triInSeg = t & 1u;

                    uint x0 = startX + seg * strideC;
                    uint x1 = x0 + strideC;

                    uint vTop0 = z1 * gVertsX + x0;
                    uint vTop1 = z1 * gVertsX + x1;

                    uint dstTri = skirtTriBase + triOffset + t;
                    uint dst = MaxVisibleIndices * cn + baseIdxCascade + dstTri * 3u;

                    uint top0 = PackIndex(vTop0, false);
                    uint top1 = PackIndex(vTop1, false);
                    uint bot0 = PackIndex(vTop0, true);
                    uint bot1 = PackIndex(vTop1, true);

                    if (triInSeg == 0)
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot1;
                        Visible_Cascades[dst + 2] = top1;
                    }
                    else
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot0;
                        Visible_Cascades[dst + 2] = bot1;
                    }
                }
                triOffset += edgeTriCount;
            }

            // West
            {
                uint edgeTriCount = numSegZ * 2u;
                uint x0_const = startX;

                for (uint t = gtid.x; t < edgeTriCount; t += 64u)
                {
                    uint seg = t >> 1;
                    uint triInSeg = t & 1u;

                    uint z0 = startZ + seg * strideC;
                    uint z1 = z0 + strideC;

                    uint vTop0 = z0 * gVertsX + x0_const;
                    uint vTop1 = z1 * gVertsX + x0_const;

                    uint dstTri = skirtTriBase + triOffset + t;
                    uint dst = MaxVisibleIndices * cn + baseIdxCascade + dstTri * 3u;

                    uint top0 = PackIndex(vTop0, false);
                    uint top1 = PackIndex(vTop1, false);
                    uint bot0 = PackIndex(vTop0, true);
                    uint bot1 = PackIndex(vTop1, true);

                    if (triInSeg == 0)
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = top1;
                        Visible_Cascades[dst + 2] = bot1;
                    }
                    else
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot1;
                        Visible_Cascades[dst + 2] = bot0;
                    }
                }
                triOffset += edgeTriCount;
            }

            // East
            {
                uint edgeTriCount = numSegZ * 2u;
                uint x1_const = startX + cellsZ;

                for (uint t = gtid.x; t < edgeTriCount; t += 64u)
                {
                    uint seg = t >> 1;
                    uint triInSeg = t & 1u;

                    uint z0 = startZ + seg * strideC;
                    uint z1 = z0 + strideC;

                    uint vTop0 = z0 * gVertsX + x1_const;
                    uint vTop1 = z1 * gVertsX + x1_const;

                    uint dstTri = skirtTriBase + triOffset + t;
                    uint dst = MaxVisibleIndices * cn + baseIdxCascade + dstTri * 3u;

                    uint top0 = PackIndex(vTop0, false);
                    uint top1 = PackIndex(vTop1, false);
                    uint bot0 = PackIndex(vTop0, true);
                    uint bot1 = PackIndex(vTop1, true);

                    if (triInSeg == 0)
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot1;
                        Visible_Cascades[dst + 2] = top1;
                    }
                    else
                    {
                        Visible_Cascades[dst + 0] = top0;
                        Visible_Cascades[dst + 1] = bot0;
                        Visible_Cascades[dst + 2] = bot1;
                    }
                }
                triOffset += edgeTriCount;
            }
        }
    }
}
