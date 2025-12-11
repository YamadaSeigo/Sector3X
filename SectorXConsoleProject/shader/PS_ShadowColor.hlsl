#include "_GlobalTypes.hlsli"
#include "_ShadowTypes.hlsli"

struct PSInputDepth
{
    float4 posH : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float2 uv : TEXCOORD3;
};

float4 main(PSInputDepth input) : SV_TARGET
{
    uint cascade = ChooseCascade(input.viewDepth);

    float4 shadowPos = mul(gLightViewProj[cascade], float4(input.worldPos, 1.0f));

    float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

    float shadowBias = 1.0f;
    if (shadowPos.z - shadow > 0.001f)
        shadowBias = 0.7f;

    if ((hasFlags & FLAG_HAS_BASECOLORTEX) == 0u)
        return baseColorFactor * shadowBias;

    float4 baseColor = gBaseColorTex.Sample(gSampler, input.uv);
    return baseColor * baseColorFactor * shadowBias;
}
