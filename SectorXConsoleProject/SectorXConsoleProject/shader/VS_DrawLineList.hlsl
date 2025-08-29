#include "GlobalTypes.hlsli"

struct VSPosInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 posH : SV_POSITION;
};

VSOutput main(VSPosInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //ŠÔÚQÆ
    row_major float4x4 model = gInstanceMats[pooledIndex];

    //float4‚ªs‚È‚Ì‚ÅŠ|‚¯‚é‰E‚©‚çŠ|‚¯‚é
    float4 worldPos = mul(float4(input.position, 1.0f), model);
    VSOutput output;
    output.posH = mul(worldPos, uViewProj);
    return output;
}