
cbuffer ViewProjectionBuffer : register(b0)
{
    float4x4 uViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;

    float4 iRow0 : INSTANCE_ROW0;
    float4 iRow1 : INSTANCE_ROW1;
    float4 iRow2 : INSTANCE_ROW2;
    float4 iRow3 : INSTANCE_ROW3;
};

