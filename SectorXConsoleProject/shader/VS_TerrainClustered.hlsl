
// ========================= COMMON TYPES =========================
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0; // 地形の基礎UV（0..1）
    float3 worldPos : TEXCOORD1; // 少なくとも x,z を使用
    float viewDepth : TEXCOORD2;
    float3 nrm : NORMAL0; // 必要なら
};


// ========================= VERTEX: VERTEX-PULL =========================

cbuffer VSParams : register(b10)
{
    row_major float4x4 View;
    row_major float4x4 Proj;
    row_major float4x4 ViewProj;
};

StructuredBuffer<uint> VisibleIndices : register(t20);
StructuredBuffer<float3> Pos : register(t21);
StructuredBuffer<float3> Nrm : register(t22);
StructuredBuffer<float2> UV : register(t23);

VSOut main(uint vtxId : SV_VertexID)
{
    uint idx = VisibleIndices[vtxId];
    float3 p = Pos[idx];
    float3 n = Nrm[idx];
    float2 uv = UV[idx];

    float4 wp = float4(p, 1);
    VSOut o;
    o.worldPos = wp.xyz;
    o.viewDepth = mul(View, wp).z;
    o.pos = mul(ViewProj, wp);
    o.uv = uv;
    o.nrm = n;

    return o;
}
