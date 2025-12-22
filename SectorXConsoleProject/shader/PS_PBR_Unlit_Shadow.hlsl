
#include "_GlobalTypes.hlsli"


cbuffer CameraBuffer : register(b11)
{
    row_major float4x4 invViewProj;
    float4 camForward; //w‚Ípadding
    float4 camPos; // w‚Ípadding
}

cbuffer LightingCB : register(b12)
{
    // Sun / directional
    float3 gSunDirectionWS;
    float gSunIntensity; // 16B
    float3 gSunColor;
    float gAmbientIntensity; // 16B

    // Ambient + counts
    float3 gAmbientColor;
    uint gPointLightCount; // 16B
};


Texture2D gAlbedoAO : register(t11);            // RGB: Albedo, A: Occlusion
Texture2D gNormalRough : register(t12);         // RGB: Normal, A: Roughness
Texture2D gEmiMetal : register(t13);            // RGB: Emissive, A: Metallic
Texture2D<float> gDepth : register(t14);        // Depth

struct PointLight
{
    float3 positionWS;
    float radius; // 16B
    float3 color;
    float invRadius; // 16B
    uint flag;
    uint3 _pad_pl;
};

StructuredBuffer<PointLight> gPointLights : register(t15);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

    float4 albedoAO = gAlbedoAO.Sample(gSampler, uv);
    float4 nr = gNormalRough.Sample(gSampler, uv);
    float4 me = gEmiMetal.Sample(gSampler, uv);

    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D‚ÌUV(ã0) ‚Æ NDC(Y+ã)‚Ì·‚ð‹zŽû
    ndc.z = gDepth.Sample(gSampler, uv);
    ndc.w = 1.0f;

    // View-Projection ‹ts—ñ‚Åƒ[ƒ‹ƒhˆÊ’u‚ð•œŒ³
    float4 wp = mul(invViewProj, ndc);

    wp /= wp.w;

    float viewDepth = dot(wp.xyz - camPos.xyz, camForward.xyz);

    uint cascade = ChooseCascade(viewDepth);

    float4 shadowPos = mul(gLightViewProj[cascade], wp);

    float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

    float shadowBias = 1.0f;
    if (shadowPos.z - shadow > 0.001f)
        shadowBias = 0.7f;

    float3 color = albedoAO.rgb * shadowBias; // ‰¼

    return float4(color, 1.0);
}
