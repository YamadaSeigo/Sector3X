// ---------------------------------------------------------------
// Terrain Clustered Vertex Shader (HeightField / ClusterGrid mode)
// ---------------------------------------------------------------

struct VSOutDepthOnly
{
    float4 pos : SV_POSITION;
};


// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z) ※同上
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)

    float2 gSplatInvSize; // 1/width, 1/height (splat texture用)

    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height
};


cbuffer VSParams : register(b11)
{
    row_major float4x4 View;
    row_major float4x4 Proj;
    row_major float4x4 ViewProj;
};

// triangle indices (generated in CS per frame)
StructuredBuffer<uint> VisibleIndices : register(t20);

// cluster grid rectangles (per-cluster)
StructuredBuffer<uint4> ClusterGridRect : register(t21);
// heightmap
Texture2D<float> HeightTex : register(t22);

SamplerState samplerLinearClamp : register(s3);

VSOutDepthOnly main(uint vtxId : SV_VertexID)
{
    const uint SKIRT_FLAG = 0x80000000u;

    uint packed = VisibleIndices[vtxId];

    bool isSkirtBottom = (packed & SKIRT_FLAG) != 0;
    uint vertexIndex = (packed & ~SKIRT_FLAG); // 下位31bit

    // Global grid (x,z)
    uint x = vertexIndex % gVertsX;
    uint z = vertexIndex / gVertsX;

    // Height/Normal UV（0..1）
    float2 uvh;
    //+0.5してテクセル中心をサンプリング
    uvh.x = ((float) x + 0.5f) * gHeightMapInvSize.x;
    uvh.y = ((float) z + 0.5f) * gHeightMapInvSize.y;

    float h = HeightTex.SampleLevel(samplerLinearClamp, uvh, 0);

    // 基本の高さ
    float baseY = offsetY + h * heightScale;

    // スカートの深さ: Q1=2 なので heightScale 比率で決定
    float skirtDepth = heightScale * 0.1f; // 好みに合わせて調整

    float3 p;
    p.x = gOriginXZ.x + (float) x * gCellSize.x;
    p.z = gOriginXZ.y + (float) z * gCellSize.y;
    p.y = isSkirtBottom ? (baseY - skirtDepth) : baseY;

    VSOutDepthOnly o;
    float4 wp = float4(p, 1.0f);

    o.pos = mul(ViewProj, wp);

    return o;
}
