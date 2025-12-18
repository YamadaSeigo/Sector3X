
#include "_GlobalTypes.hlsli"

Texture2D gAlbedoAO : register(t11);
Texture2D gNormalRough : register(t12);
Texture2D gMetalEmi : register(t13);
Texture2D<float> gDepth : register(t14); // depth SRV化している想定

cbuffer CameraBuffer : register(b11)
{
    row_major float4x4 invViewProj;
    float4 camForward; //wはpadding
    float4 camPos; // wはpadding
}

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
    float4 me = gMetalEmi.Sample(gSampler, uv);

    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3DのUV(上0) と NDC(Y+上)の差を吸収
    ndc.z = gDepth.Sample(gSampler, uv);
    ndc.w = 1.0f;

    // View-Projection 逆行列でワールド位置を復元
    float4 wp = mul(invViewProj, ndc);

    wp /= wp.w;

    float viewDepth = dot(wp.xyz - camPos.xyz, camForward.xyz);

    uint cascade = ChooseCascade(viewDepth);

    float4 shadowPos = mul(gLightViewProj[cascade], wp);

    float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

    float shadowBias = 1.0f;
    if (shadowPos.z - shadow > 0.001f)
        shadowBias = 0.7f;

    float3 color = albedoAO.rgb * shadowBias; // 仮

    return float4(color, 1.0);
}
