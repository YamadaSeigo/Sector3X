
// ========================= COMMON TYPES =========================
struct VSOutDepthOnly
{
    float4 pos : SV_POSITION;
};


// ========================= VERTEX: VERTEX-PULL =========================

cbuffer VSParams : register(b10)
{
    row_major float4x4 ViewProj;
    row_major float4x4 World;
};

StructuredBuffer<uint> VisibleIndices : register(t20);
StructuredBuffer<float3> Pos : register(t21);


VSOutDepthOnly main(uint vtxId : SV_VertexID)
{
    uint idx = VisibleIndices[vtxId];
    float3 p = Pos[idx];

    float4 wp = mul(World, float4(p, 1));
    VSOutDepthOnly o;
    o.pos = mul(ViewProj, wp);

    return o;
}
