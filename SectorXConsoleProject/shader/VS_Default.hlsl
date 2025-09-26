#include "GlobalTypes.hlsli"

struct VSOutput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //ä‘ê⁄éQè∆
    row_major float4x4 model = gInstanceMats[pooledIndex];

    //float4Ç™çsÇ»ÇÃÇ≈ä|ÇØÇÈâEÇ©ÇÁä|ÇØÇÈ
    float4 worldPos = mul(float4(input.position, 1.0f), model);
    VSOutput output;
    output.posH = mul(worldPos, uViewProj);
    output.uv = input.uv;
    output.normal = normalize(mul(input.normal, (float3x3) model));
    return output;
}