
#include "_GlobalTypes.hlsli"

cbuffer CBUpdate : register(b10)
{
    float gDt;
    float gTime;
    float gGravity;

    float padding0;

    float3 gCamPosWS;
    float gSpawnRadius;

    float3 gWindWS;

    float padding1;

    row_major float4x4 gCamViewProj;
    float2 gRainInvMapSize; // (1/width, 1/height)
    float gRainDepthBias;
    float padding2;
};

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID) : SV_POSITION
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