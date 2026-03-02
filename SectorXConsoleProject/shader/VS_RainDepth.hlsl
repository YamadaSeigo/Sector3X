
#include "_GlobalTypes.hlsli"

cbuffer MatrixCB : register(b10)
{
    row_major float4x4 gCamViewProj;
};

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //ä‘ê⁄éQè∆

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    const float3 wp = mul(R, input.position) + t;

    VSOutput output;
    output.position = mul(gCamViewProj, float4(wp, 1));

	return output;
}