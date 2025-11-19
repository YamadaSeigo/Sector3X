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

    //shadowPos.xyz = shadowPos.xyz * 0.5f + 0.5f;

    //return float4(shadowPos.x, 0, shadowPos.z, 1);

    float shadow = DebugShadowDepth(input.worldPos, cascade);

    float shadowBias = 1.0f;
    if (shadowPos.z - shadow > 0.1f)
        shadowBias = 0.5f;

    // カスケードシャドウのサンプル
    //float shadow = SampleShadow(input.worldPos, input.viewDepth);

    //return float4(shadow, 0/*(float) cascade / NUM_CASCADES*/, 0, 1);

    if (hasBaseColorTex == 0)
        return baseColorFactor * shadowBias;

    float4 baseColor = gBaseColorTex.Sample(gSampler, input.uv);
    return baseColor * baseColorFactor * shadowBias;
}
