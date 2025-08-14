#include "GlobalTypes.hlsli"


struct VSOutput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

VSOutput main(VSInput input)
{
    row_major float4x4 model = float4x4(
        input.iRow0,
        input.iRow1,
        input.iRow2,
        input.iRow3
    );

    //float4Ç™çsÇ»ÇÃÇ≈ä|ÇØÇÈâEÇ©ÇÁä|ÇØÇÈ
    float4 worldPos = mul(model, float4(input.position, 1.0f));
    VSOutput output;
    output.posH = mul(worldPos, uViewProj);
    output.uv = input.uv;
    output.normal = baseColorFactor.xyz; //normalize(mul((float3x3)model, input.normal));
    return output;
}