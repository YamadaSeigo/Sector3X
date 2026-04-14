// ---------------------------------------------------------------
// Terrain Clustered Vertex Shader (HeightField + Full Skirt)
// ---------------------------------------------------------------

struct VSOut
{
    float4 pos : SV_Position;
    float3 posWS : TEXCOORD0;
    float3 posVS : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float2 uv : TEXCOORD3; // 0..1 (normal texture 用)
};

// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float3 gOrigin; // ワールド座標の基準 (x,z)
    float heightScale;

    uint gVertsX; // Heightfield 全体の頂点数X
    uint gVertsZ; // Heightfield 全体の頂点数Z

    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z

    uint gCellsX; // クラスタあたりのセル数X
    uint gCellsZ; // クラスタあたりのセル数Z

    float2 gCellInvCount; // 1/(cell count)

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
Texture2D<float> HeightTex : register(t21);

SamplerState samplerLinearClamp : register(s3);

VSOut main(uint vtxId : SV_VertexID)
{
    uint vid = VisibleIndices[vtxId];

    // Global grid (x,z)
    uint x = vid % gVertsX;
    uint z = vid / gVertsX;

    // Height UV（0..1）
    float2 uvh;
    uvh.x = ((float) x + 0.5f) * gHeightMapInvSize.x;
    uvh.y = ((float) z + 0.5f) * gHeightMapInvSize.y;

    uint cx = x % gCellsX + 1;
    uint cz = z % gCellsZ + 1;

    float2 uvn;
    uvn.x = (float) x / gCellsX;
    uvn.y = (float) z / gCellsZ;

    float h = HeightTex.SampleLevel(samplerLinearClamp, uvh, 0);

    float3 p;
    p.x = gOrigin.x + (float) x * gCellSize.x;
    p.z = gOrigin.z + (float) z * gCellSize.y;
    p.y = gOrigin.y + h * heightScale;

    VSOut o;
    float4 wp = float4(p, 1.0f);

    o.posWS = p;
    o.posVS = mul(View, wp).xyz;
    o.viewDepth = mul(View, wp).z;
    o.pos = mul(ViewProj, wp);
    o.uv = uvn;

    return o;
}
