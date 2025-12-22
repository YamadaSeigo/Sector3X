#include "_GlobalTypes.hlsli"

cbuffer CameraBuffer : register(b11)
{
    row_major float4x4 invViewProj;
    float4 camForward; // w padding
    float4 camPos; // w padding
};

cbuffer LightingCB : register(b12)
{
    // Sun / directional
    float3 gSunDirectionWS; // 「光が進む方向」だと仮定（太陽光線の方向）
    float gSunIntensity;
    float3 gSunColor;
    float gAmbientIntensity;

    // Ambient + counts
    float3 gAmbientColor;
    uint gPointLightCount;
};

Texture2D gAlbedoAO : register(t11); // RGB: Albedo, A: Occlusion
Texture2D gNormalRough : register(t12); // RGB: Normal(encode), A: Roughness
Texture2D gEmiMetal : register(t13); // RGB: Emissive, A: Metallic
Texture2D<float> gDepth : register(t14); // Depth (0..1)

struct PointLight
{
    float3 positionWS;
    float radius; // 16B
    float3 color;
    float invRadius; // 1/radius
    uint flag;
    uint3 _pad_pl;
};

StructuredBuffer<PointLight> gPointLights : register(t15);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// -------------------------
// PBR helpers (GGX)
// -------------------------

static const float PI = 3.14159265f;

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * Pow5(1.0f - saturate(cosTheta));
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = max(roughness, 0.045f); // 極端なスパイク防止
    float a2 = a * a;

    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 1e-6f);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f; // UE4近似

    return NdotX / max(NdotX * (1.0f - k) + k, 1e-6f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggxV = GeometrySchlickGGX(NdotV, roughness);
    float ggxL = GeometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

// 距離減衰（シンプルで見た目が良いタイプ）
float Attenuation_Point(float dist, float radius)
{
    float x = saturate(1.0f - dist / max(radius, 1e-6f));
    // なだらかに落ちる（二乗×二乗）
    return x * x * x * x;
}

// -------------------------

float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

    // Depth=1 は背景として早期リターン（環境描画が別ならここで黒）
    float depth = gDepth.Sample(gSampler, uv);
    if (depth >= 0.999999f)
        return float4(0, 0, 0, 1);

    // GBuffer
    float4 albedoAO = gAlbedoAO.Sample(gSampler, uv);
    float4 nr = gNormalRough.Sample(gSampler, uv);
    float4 em = gEmiMetal.Sample(gSampler, uv);

    float3 albedo = albedoAO.rgb;
    float ao = saturate(albedoAO.a);
    float3 emissive = em.rgb;
    float metallic = saturate(em.a);
    float roughness = saturate(nr.a);

    // Normal decode: (0..1)->(-1..1) を仮定
    float3 N = normalize(nr.rgb * 2.0f - 1.0f);

    // WorldPos reconstruct
    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D UVとNDCの上下差
    ndc.z = depth; // 0..1
    ndc.w = 1.0f;

    float4 wp4 = mul(invViewProj, ndc);
    wp4 /= max(wp4.w, 1e-6f);
    float3 worldPos = wp4.xyz;

    // View vector
    float3 V = normalize(camPos.xyz - worldPos);

    // 視線方向(カメラ前方)で viewDepth を作って cascade 選択
    float viewDepth = dot(worldPos - camPos.xyz, camForward.xyz);

    // F0
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // エネルギー保存
    float3 Lo = 0.0f;

    // -------------------------
    // Directional (Sun)
    // -------------------------
    float3 Ld = normalize(-gSunDirectionWS); // “光が進む方向”想定なので反転して点→光へ
    float3 Hd = normalize(V + Ld);

    float NdotL = saturate(dot(N, Ld));
    if (NdotL > 0.0f)
    {
        float D = DistributionGGX(N, Hd, roughness);
        float G = GeometrySmith(N, V, Ld, roughness);
        float3 F = FresnelSchlick(saturate(dot(Hd, V)), F0);

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);

        float3 diffuse = kD * albedo / PI;
        float3 specular = (D * G * F) / max(4.0f * saturate(dot(N, V)) * NdotL, 1e-6f);

        float3 radiance = gSunColor * gSunIntensity;

        // 影（Directionalのみ）
        //float shadow = SampleShadow(worldPos, viewDepth); // 1=光あり, 0=影

        float viewDepth = dot(wp4.xyz - camPos.xyz, camForward.xyz);

        uint cascade = ChooseCascade(viewDepth);

        float4 shadowPos = mul(gLightViewProj[cascade], wp4);

        float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

        float shadowBias = 1.0f;
        if (shadowPos.z - shadow > 0.001f)
            shadowBias = 0.0f;

        Lo += (diffuse + specular) * radiance * NdotL * shadowBias;
    }

    // -------------------------
    // Point lights
    // -------------------------
    [loop]
    for (uint li = 0; li < gPointLightCount; ++li)
    {
        PointLight pl = gPointLights[li];

        float3 toL = pl.positionWS - worldPos;
        float dist = length(toL);

        // 範囲外はスキップ
        if (dist >= pl.radius)
            continue;

        float3 L = toL / max(dist, 1e-6f);
        float3 H = normalize(V + L);

        float NdotL2 = saturate(dot(N, L));
        if (NdotL2 <= 0.0f)
            continue;

        float att = Attenuation_Point(dist, pl.radius);
        float3 radiance = pl.color * att; // 必要なら強度を別で掛ける

        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);

        float3 diffuse = kD * albedo / PI;
        float3 specular = (D * G * F) / max(4.0f * saturate(dot(N, V)) * NdotL2, 1e-6f);

        Lo += (diffuse + specular) * radiance * NdotL2;
    }

    // -------------------------
    // Ambient (簡易)
    // ※IBLが無い前提の “それっぽい” 基礎。あとでIBLに置き換え推奨。
    // -------------------------
    float3 ambient = (albedo * gAmbientColor) * gAmbientIntensity * ao;

    float3 color = ambient + Lo + emissive;

    return float4(color, 1.0f);
}
