#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    return gBaseColorTex.Sample(gSampler, input.uv);
}
