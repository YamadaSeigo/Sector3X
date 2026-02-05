#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 color = gBaseColorTex.Sample(gSampler, input.uv) * baseColorFactor;

    color.rbg += color.a;
    color.a = 1.0f;

    return color;

}
