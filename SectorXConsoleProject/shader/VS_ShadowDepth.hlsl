#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VS_OUT
{
    float4 posH : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float2 uv : TEXCOORD3;
};


VS_OUT main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VS_OUT output;
    const float3 wp = mul(R, input.position) + t;
    output.worldPos = wp;

    float3 nW = mul(R, input.normal); // 非一様スケール無し前提

    // クリップ座標
    float4 viewPos = mul(uView, float4(wp, 1.0));
    output.viewDepth = viewPos.z;

    output.posH = mul(uViewProj, float4(wp, 1.0));
    output.uv = input.uv;
    output.normalWS = nW;
    return output;
}