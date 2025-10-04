#include "GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{        
    if (hasBaseColorTex == 0)
        return baseColorFactor;

    float4 baseColor = gBaseColorTex.Sample(gSampler, input.uv);
    return baseColor * baseColorFactor;
}
