// ---------------------------------------------------------------
// Terrain Patch Depth VS (procedural grid from SV_VertexID)
// Inputs: CB + HeightTex + Sampler only
// Draw call: Draw( (gPatchVertsX-1)*(gPatchVertsZ-1)*6, 0 )
// ---------------------------------------------------------------

struct VSOutDepthOnly
{
    float4 pos : SV_POSITION;
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

cbuffer PatchCB : register(b11)
{
    // --- patch controls ---
    float2 gPatchCenterXZ; // center in world (x,z)
    float gPatchHalfSize; // half-size in meters (square)

    float padding; // CB size must be multiple of 16 bytes

    uint gPatchVertsX; // patch vertex resolution X (>=2)
    uint gPatchVertsZ; // patch vertex resolution Z (>=2)

    float2 padding2; // CB size must be multiple of 16 bytes
};

cbuffer VSParams : register(b12)
{
    row_major float4x4 ViewProj;
};

Texture2D<float> HeightTex : register(t22);
SamplerState samplerLinearClamp : register(s3);

static const uint2 kTriLUT[6] =
{
    uint2(0, 0), uint2(0, 1), uint2(1, 0), // tri 0
    uint2(1, 0), uint2(0, 1), uint2(1, 1) // tri 1
};

VSOutDepthOnly main(uint vtxId : SV_VertexID)
{
    VSOutDepthOnly o;

    uint cellsX = (gPatchVertsX > 1) ? (gPatchVertsX - 1) : 0;
    uint cellsZ = (gPatchVertsZ > 1) ? (gPatchVertsZ - 1) : 0;

    // safety
    if (cellsX == 0 || cellsZ == 0)
    {
        o.pos = float4(0, 0, 0, 1);
        return o;
    }

    uint cellId = vtxId / 6;
    uint triVid = vtxId % 6;

    uint cx = cellId % cellsX;
    uint cz = cellId / cellsX;

    uint2 off = kTriLUT[triVid];

    // vertex index in patch grid [0..gPatchVertsX-1], [0..gPatchVertsZ-1]
    uint vx = cx + off.x;
    uint vz = cz + off.y;

    // world XZ (square centered at gPatchCenterXZ)
    float2 patchMin = gPatchCenterXZ - float2(gPatchHalfSize, gPatchHalfSize);
    float2 patchMax = gPatchCenterXZ + float2(gPatchHalfSize, gPatchHalfSize);

    float2 t;
    t.x = (gPatchVertsX > 1) ? ((float) vx / (float) (gPatchVertsX - 1)) : 0.0f;
    t.y = (gPatchVertsZ > 1) ? ((float) vz / (float) (gPatchVertsZ - 1)) : 0.0f;

    float2 worldXZ = lerp(patchMin, patchMax, t);

    // map worldXZ -> terrain UV (0..1)
    float2 terrainSizeXZ = float2((gVertsX - 1) * gCellSize.x,
                                  (gVertsZ - 1) * gCellSize.y);

    float2 uv = (worldXZ - gOriginXZ) / terrainSizeXZ;

    // clamp to avoid sampling outside
    uv = saturate(uv);

    float h = HeightTex.SampleLevel(samplerLinearClamp, uv, 0);
    float y = offsetY + h * heightScale;

    float4 wp = float4(worldXZ.x, y, worldXZ.y, 1.0f);
    o.pos = mul(ViewProj, wp);
    return o;
}