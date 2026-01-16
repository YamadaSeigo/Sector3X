#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 baseColor = baseColorFactor;
    if ((hasFlags & FLAG_HAS_BASECOLORTEX) != 0u)
        baseColor *= gBaseColorTex.Sample(gSampler, input.uv);

    return baseColor * float4(1.0f, 1.0f, 1.0f, 0.4f);
}
