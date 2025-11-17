#include "_GlobalTypes.hlsli"

struct VSInputPos
{
    float3 position : POSITION;
};

struct VSOutputPos
{
    float4 clip : SV_POSITION;
};

VSOutputPos main(VSInputPos input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VSOutputPos output;
    const float3 wp = mul(R, input.position) + t;

    // クリップ座標
    output.clip = mul(uViewProj, float4(wp, 1.0));
    return output;
}