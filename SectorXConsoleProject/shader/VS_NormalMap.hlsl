#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 posCS : SV_POSITION;
    float3 T_ws : TEXCOORD0;
    float3 B_ws : TEXCOORD1;
    float3 N_ws : TEXCOORD2;
    float2 uv : TEXCOORD4;
};


VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //ä‘ê⁄éQè∆

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    const float3 wp = mul(R, input.position) + t;

    output.posCS = mul(uViewProj, float4(wp, 1.0));
    output.N_ws = normalize(mul(R, input.normal));
    output.T_ws = normalize(mul(R, input.tangent.xyz));
    output.B_ws = cross(output.N_ws, output.T_ws) * input.tangent.w;
    output.uv = input.uv;
    return output;
}