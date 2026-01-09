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
    float3 gSunDirectionWS;
    float gSunIntensity;
    float3 gSunColor;
    float gAmbientIntensity;

    // Ambient + counts
    float3 gAmbientColor;
    uint gPointLightCount;
    
    float emissiveBoost; // Emissiveの強調係数
    float3 _pad3;
};

// Stylized 用パラメータ
cbuffer StylizedCB : register(b13)
{
    // Toon diffuse
    float gToonSteps; // 例: 4.0
    float gToonSoftness; // 例: 0.05 (境界のぼかし)
    float gWrap; // 例: 0.2 (ハーフランバート寄りにする)

    // Spec band
    float gSpecPower; // 例: 64
    float gSpecThreshold; // 例: 0.5
    float gSpecSoftness; // 例: 0.08
    float gSpecIntensity; // 例: 1.0

    // Rim
    float gRimPower; // 例: 3.0
    float gRimIntensity; // 例: 0.6
    float3 gRimColor; // 例: (1,1,1)

    // Optional outline (0:off, 1:on)
    uint gEnableOutline;
    float gOutlineDepthScale; // 例: 80.0 (強さ)
    float gOutlineNormalScale; // 例: 1.5
    float gOutlineIntensity; // 例: 1.0

    float2 gInvRTSize; // 例: (1/width, 1/height)
    float2 _pad_st;
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

static const float PI = 3.14159265f;

// -------------------------
// PBR helpers (GGX)  (元のまま)
// -------------------------
float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * Pow5(1.0f - cosTheta);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = max(roughness, 0.045f);
    float a2 = a * a;

    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 1e-6f);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float Attenuation_Point(float dist, float radius)
{
    float x = saturate(1.0f - dist / max(radius, 1e-6f));
    return x * x * x * x;
}

// -------------------------
// ★Stylized helpers
// -------------------------
float ToonStep(float x, float steps, float softness)
{
    // x: 0..1
    x = saturate(x);
    steps = max(1.0f, steps);
    float q = floor(x * steps) / steps; // 段階化
    // 境界付近だけ少しなめらかに
    float edge = abs(x - q);
    float s = smoothstep(0.0f, softness, edge);
    return lerp(q, x, s);
}

float ToonDiffuseNdotL(float NdotL)
{
    // wrap (ハーフランバート寄り)
    float w = saturate((NdotL + gWrap) / (1.0f + gWrap));
    return ToonStep(w, gToonSteps, gToonSoftness);
}

float ToonSpec(float3 N, float3 V, float3 L)
{
    float3 H = normalize(V + L);
    float s = pow(saturate(dot(N, H)), max(1.0f, gSpecPower));
    // バンド化
    float band = smoothstep(gSpecThreshold - gSpecSoftness, gSpecThreshold + gSpecSoftness, s);
    return band * gSpecIntensity;
}

float RimTerm(float3 N, float3 V)
{
    float rim = 1.0f - saturate(dot(N, V));
    return pow(rim, max(0.001f, gRimPower)) * gRimIntensity;
}

// 任意: 簡易アウトライン（深度＋法線差分）
float OutlineFactor(float2 uv, float depthCenter, float3 nCenter)
{
    if (gEnableOutline == 0)
        return 0.0f;

    float2 dx = float2(gInvRTSize.x, 0.0f);
    float2 dy = float2(0.0f, gInvRTSize.y);

    float dR = gDepth.Sample(gSampler, uv + dx);
    float dL = gDepth.Sample(gSampler, uv - dx);
    float dU = gDepth.Sample(gSampler, uv - dy);
    float dD = gDepth.Sample(gSampler, uv + dy);

    float3 nR = normalize(gNormalRough.Sample(gSampler, uv + dx).rgb * 2.0f - 1.0f);
    float3 nL = normalize(gNormalRough.Sample(gSampler, uv - dx).rgb * 2.0f - 1.0f);
    float3 nU = normalize(gNormalRough.Sample(gSampler, uv - dy).rgb * 2.0f - 1.0f);
    float3 nD = normalize(gNormalRough.Sample(gSampler, uv + dy).rgb * 2.0f - 1.0f);

    float depthEdge =
        abs(dR - depthCenter) + abs(dL - depthCenter) +
        abs(dU - depthCenter) + abs(dD - depthCenter);

    float normalEdge =
        (1.0f - saturate(dot(nR, nCenter))) + (1.0f - saturate(dot(nL, nCenter))) +
        (1.0f - saturate(dot(nU, nCenter))) + (1.0f - saturate(dot(nD, nCenter)));

    float e = depthEdge * gOutlineDepthScale + normalEdge * gOutlineNormalScale;
    return saturate(e) * gOutlineIntensity;
}

// -------------------------

float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

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

    float3 N = normalize(nr.rgb * 2.0f - 1.0f);

    // WorldPos reconstruct
    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    ndc.z = depth;
    ndc.w = 1.0f;

    float4 wp4 = mul(invViewProj, ndc);
    wp4 /= max(wp4.w, 1e-6f);
    float3 worldPos = wp4.xyz;

    float3 V = normalize(camPos.xyz - worldPos);

    // PBR F0（残す）
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 Lo = 0.0f;

    // -------------------------
    // Directional (Sun) : PBR→Stylized化して加算
    // -------------------------
    float3 Ld = normalize(-gSunDirectionWS);
    float NdotL = saturate(dot(N, Ld));
    if (NdotL > 0.0f)
    {
        // 元のPBR成分（必要なら残す）
        float3 Hd = normalize(V + Ld);
        float D = DistributionGGX(N, Hd, roughness);
        float G = GeometrySmith(N, V, Ld, roughness);
        float3 F = FresnelSchlick(saturate(dot(Hd, V)), F0);

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);

        float3 diffusePBR = kD * albedo / PI;
        float3 specularPBR = (D * G * F) / max(4.0f * saturate(dot(N, V)) * NdotL, 1e-6f);

        // ★Stylized係数（拡散は段階化、スペキュラはバンド化）
        float toonNL = ToonDiffuseNdotL(NdotL);
        float toonSp = ToonSpec(N, V, Ld);

        // “PBRを残しつつトゥーン化”の混ぜ方例：
        float3 diffuse = diffusePBR * toonNL;
        float3 specular = (specularPBR * toonSp);

        float3 radiance = gSunColor * gSunIntensity;

        // リム（太陽色に寄せてもよい）
        float rim = RimTerm(N, V);
        float3 rimCol = gRimColor * rim * radiance;

        // 視線方向(カメラ前方)で viewDepth を作って cascade 選択
        float viewDepth = dot(worldPos - camPos.xyz, camForward.xyz);

        uint cascade = ChooseCascade(viewDepth);

        float4 shadowPos = mul(gLightViewProj[cascade], wp4);

        float shadow = GetShadowMapDepth(shadowPos.xyz, cascade);

        float shadowBias = 1.0f;
        if (shadowPos.z - shadow > 0.001f)
            shadowBias = 0.0f;

        Lo += (diffuse + specular) * radiance * shadowBias + rimCol;
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
        if (dist > pl.radius)
            continue;

        float3 L = toL / max(dist, 1e-6f);
        float NdotL2 = saturate(dot(N, L));
        if (NdotL2 <= 0.0f)
            continue;

        float atten = Attenuation_Point(dist, pl.radius);
        float3 radiance = pl.color * atten;

        // PBR base
        float3 H = normalize(V + L);
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);

        float3 diffusePBR = kD * albedo / PI;
        float3 specularPBR = (D * G * F) / max(4.0f * saturate(dot(N, V)) * NdotL2, 1e-6f);

        // Stylized
        float toonNL = ToonDiffuseNdotL(NdotL2);
        float toonSp = ToonSpec(N, V, L);

        float3 diffuse = diffusePBR * toonNL;
        float3 specular = specularPBR * toonSp;

        float rim = RimTerm(N, V);
        float3 rimCol = gRimColor * rim * radiance;

        Lo += (diffuse + specular) * radiance + rimCol;
    }

    // -------------------------
    // Ambient : “塗り”っぽくする（陰影を弱める/強める等）
    // -------------------------
    // 例: AOで締めつつ、少しフラット寄りに
    float3 ambient = (albedo * gAmbientColor) * gAmbientIntensity * ao;

    float3 color = ambient + Lo + emissive;

    // 任意: アウトライン（黒を乗算/減算）
    float outline = OutlineFactor(uv, depth, N);
    color *= (1.0f - outline);

    return float4(color, 1.0f);
}
