// ---------------------------------------------------------------
// Terrain Clustered Vertex Shader (HeightField + Full Skirt)
// ---------------------------------------------------------------

struct VSOut
{
    float4 pos : SV_Position;
    float3 worldPos : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float3 nrm : NORMAL0;
};

// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    uint gVertsX; // Heightfield 全体の頂点数X
    uint gVertsZ; // Heightfield 全体の頂点数Z

    float2 gSplatInvSize; // 1/width, 1/height (splat texture用)
    
    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height (height/normal texture用)
};

cbuffer VSParams : register(b11)
{
    row_major float4x4 View;
    row_major float4x4 Proj;
    row_major float4x4 ViewProj;
};

// triangle indices (generated in CS per frame)
StructuredBuffer<uint> VisibleIndices : register(t20);

// heightmap & normalmap
Texture2D<float> HeightTex : register(t22);
Texture2D<float2> NormalTex : register(t23);

SamplerState samplerLinearClamp : register(s3);

VSOut main(uint vtxId : SV_VertexID)
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
    uvh.x = ((float) x + 0.5f) * gHeightMapInvSize.x;
    uvh.y = ((float) z + 0.5f) * gHeightMapInvSize.y;

    float h = HeightTex.SampleLevel(samplerLinearClamp, uvh, 0);

    float2 enc = NormalTex.SampleLevel(samplerLinearClamp, uvh, 0);
    float2 xz = enc * 2.0f - 1.0f; // -1..1

    float nx = xz.x;
    float nz = xz.y;

    float ny2 = saturate(1.0f - nx * nx - nz * nz);
    float ny = sqrt(ny2); // 上向き前提なので +sqrt

    float3 n =  normalize(float3(nx, ny, nz));

    //float3 n = NormalTex.SampleLevel(samplerLinearClamp, uvh, 0);
    //n = normalize(n);

    // 基本の高さ
    float baseY = offsetY + h * heightScale;

    // スカートの深さ: Q1=2 なので heightScale 比率で決定
    float skirtDepth = heightScale * 0.1f; // 好みに合わせて調整

    float3 p;
    p.x = gOriginXZ.x + (float) x * gCellSize.x;
    p.z = gOriginXZ.y + (float) z * gCellSize.y;
    p.y = isSkirtBottom ? (baseY - skirtDepth) : baseY;

    VSOut o;
    float4 wp = float4(p, 1.0f);

    o.worldPos = p;
    o.viewDepth = mul(View, wp).z;
    o.pos = mul(ViewProj, wp);
    o.nrm = n; // スカート底もとりあえず同じ normal

    return o;
}
