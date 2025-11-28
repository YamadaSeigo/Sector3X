#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 baseColor = baseColorFactor;
    if ((hasFlags & FLAG_HAS_BASECOLORTEX) != 0u)
        baseColor *= gBaseColorTex.Sample(gSampler, input.uv);

    float4 emissionColor = float4(0, 0, 0, 0);
    if ((hasFlags & FLAG_HAS_EMISSIVETEX) != 0u)
        emissionColor = gEmissiveTex.Sample(gSampler, input.uv);

    return baseColor + emissionColor;
}
