
// ========================= COMMON TYPES =========================
struct VSOut
{
    float4 pos : SV_Position;
    float3 n : NORMAL;
    float2 uv : TEXCOORD0;
};


// ========================= VERTEX: VERTEX-PULL =========================

cbuffer VSParams : register(b0)
{
    row_major float4x4 ViewProj;
    row_major float4x4 World;
};

StructuredBuffer<uint> VisibleIndices : register(t0);
StructuredBuffer<float3> Pos : register(t1);
StructuredBuffer<float3> Nrm : register(t2);
StructuredBuffer<float2> UV : register(t3);

VSOut main(uint vtxId : SV_VertexID)
{
    uint idx = VisibleIndices[vtxId];
    float3 p = Pos[idx];
    float3 n = Nrm[idx];
    float2 uv = UV[idx];

    float4 wp = mul(World, float4(p, 1));
    VSOut o;
    o.pos = mul(ViewProj, wp);
    o.n = normalize(mul(World, float4(n, 0)).xyz);
    o.uv = uv;
    return o;
}
