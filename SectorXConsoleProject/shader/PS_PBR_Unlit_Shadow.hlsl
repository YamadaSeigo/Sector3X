
#include "_GlobalTypes.hlsli"


cbuffer CameraBuffer : register(b11)
{
    row_major float4x4 invViewProj;
    float4 camForward; //wはpadding
    float4 camPos; // wはpadding
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

// shadowPos は LightViewProj 後の clip（もしくは同等）を想定
float SampleShadowPCF(float4 shadowClip, uint cascade)
{
    // clip -> ndc -> uv
    float3 ndc = shadowClip.xyz / shadowClip.w;
    float2 uv = ndc.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y; // D3DのUV(上0) と NDC(Y+上)の差を吸収
    float z = ndc.z;

    // 範囲外は「影なし」扱い（好みで 0 にしてもOK）
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return 1.0f;

    // 解像度から1texelを取得
    uint w, h, layers;
    gShadowMap.GetDimensions(w, h, layers);
    float2 texel = 1.0f / float2(w, h);

    // 3x3 PCF
    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * texel;
            sum += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + o, cascade), z);
        }
    }
    return sum / 9.0f; // 1=光が当たる, 0=完全に影
}

float ComputeFogFactor(float viewDepth)
{
    // viewDepth: カメラから前方向の距離(メートル想定)。負になり得るなら abs/viewDepth clamp を入れる
    viewDepth = max(viewDepth, 0.0f);


    float denom = max(3000.0f /*FogEnd*/ - 100.0f /*FogStart*/, 1e-5f);
    return saturate((viewDepth - 100.0f /*FogStart*/) / denom);
}

float ComputeHeightFogFactor(float3 camPosWS, float3 worldPosWS, float viewDepth)
{
    //if (gEnableHeightFog == 0)
    //    return 0.0f;

    viewDepth = max(viewDepth, 0.0f);

    // 高さを基準高さからの相対にする（base より上ほど薄く）
    float camY = camPosWS.y - 1.0f/*gHeightFogBaseHeight*/;
    float posY = worldPosWS.y - 1.0f/*gHeightFogBaseHeight*/;

    float k = max(0.05f/*gHeightFogFalloff*/, 1e-5f);

    // density(y) = exp(-k * y)
    float e0 = exp(-k * camY);
    float e1 = exp(-k * posY);

    float dy = (worldPosWS.y - camPosWS.y);

    // レイに沿った exp の平均（dy が小さいときは中点近似）
    float avgExp;
    if (abs(dy) < 1e-4f)
    {
        float midY = 0.5f * (camY + posY);
        avgExp = exp(-k * midY);
    }
    else
    {
        // (e0 - e1) / (k * dy) は「高さ方向の変化を考慮した平均」になりやすい
        avgExp = (e0 - e1) / (k * dy);
        // 数値が暴れた時の保険
        avgExp = max(avgExp, 0.0f);
    }

    // optical depth（霧の濃さ×距離×平均密度）
    float opticalDepth = 0.01f/*gHeightFogDensity*/ * viewDepth * avgExp;

    // 0..1 のフォグ係数
    return saturate(1.0f - exp(-opticalDepth));
}



float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

    float4 albedoAO = gAlbedoAO.Sample(gSampler, uv);
    float4 nr = gNormalRough.Sample(gSampler, uv);
    float4 me = gEmiMetal.Sample(gSampler, uv);

    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3DのUV(上0) と NDC(Y+上)の差を吸収
    ndc.z = gDepth.Sample(gSampler, uv);
    ndc.w = 1.0f;

    // View-Projection 逆行列でワールド位置を復元
    float4 wp = mul(invViewProj, ndc);

    wp /= wp.w;

    // ベースの「夜の明るさ」（全体暗さ）
    float3 ambient = albedoAO.rgb * (gAmbientColor * gAmbientIntensity);

    float viewDepth = dot(wp.xyz - camPos.xyz, camForward.xyz);

    uint cascade = ChooseCascade(viewDepth);

    float4 shadowClip = mul(gLightViewProj[cascade], wp);
    float vis = SampleShadowPCF(shadowClip, cascade);

    // 影の濃さ(0.7)は元の意図を踏襲
    float shadowMul = lerp(0.6f, 1.0f, vis);

    float3 color = ambient * shadowMul;

    // 距離フォグ
    float fogDist = ComputeFogFactor(viewDepth);

    // 高さフォグ
    float fogHeight = ComputeHeightFogFactor(camPos.xyz, wp.xyz, viewDepth);

    // 2つのフォグを合体：1 - (1-a)(1-b) が自然に「どっちも効く」
    float fog = 1.0f - /*(1.0f - ) * */(1.0f - fogHeight * fogDist);

    color = lerp(color, float3(0.8f, 0.8f, 1.0f) /*gFogColor*/, fog);

    return float4(color, 1.0f);
}
