#include "GlobalTypes.hlsli"

struct VSOutput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    float4x4 model = float4x4(
        input.iRow0,
        input.iRow1,
        input.iRow2,
        input.iRow3
    );

    float4 worldPos = mul(float4(input.position, 1.0), model);
    VSOutput output;
    output.posH = mul(worldPos, uViewProj);
    output.uv = input.uv;
    return output;
}