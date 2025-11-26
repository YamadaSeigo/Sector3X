#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    const float3 wp = mul(R, input.position) + t;
    float3 nW = mul(R, input.normal); // 非一様スケール無し前提

    // クリップ座標
    output.clip = mul(uViewProj, float4(wp, 1.0));
    output.uv = input.uv;
    output.normal = nW;
    return output;
}