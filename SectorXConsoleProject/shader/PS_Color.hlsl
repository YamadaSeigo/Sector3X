#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.color *= gBaseColorTex.Sample(gSampler, input.uv);
}
