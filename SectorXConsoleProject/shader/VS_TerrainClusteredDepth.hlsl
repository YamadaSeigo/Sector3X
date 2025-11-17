
// ========================= COMMON TYPES =========================
struct VSOut
{
    float4 pos : SV_Position;
};


// ========================= VERTEX: VERTEX-PULL =========================

cbuffer VSParams : register(b10)
{
    row_major float4x4 ViewProj;
    row_major float4x4 World;
};

StructuredBuffer<uint> VisibleIndices : register(t20);
StructuredBuffer<float3> Pos : register(t21);


VSOut main(uint vtxId : SV_VertexID)
{
    uint idx = VisibleIndices[vtxId];
    float3 p = Pos[idx];

    float4 wp = mul(World, float4(p, 1));
    VSOut o;
    o.pos = mul(ViewProj, wp);

    return o;
}
