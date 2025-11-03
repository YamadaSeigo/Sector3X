// ========================= COMMON TYPES =========================
struct VSOut
{
    float4 pos : SV_Position;
    float3 n : NORMAL;
    float2 uv : TEXCOORD0;
};

// ========================= PIXEL: SIMPLE =========================
Texture2D AlbedoTex : register(t0);
SamplerState Samp : register(s0);

float4 main(VSOut i) : SV_Target
{
    return float4(1, 0, 1, 1);

    float3 albedo = AlbedoTex.Sample(Samp, i.uv).rgb;
    float3 L = normalize(float3(0.5, 1.0, 0.2));
    float NdotL = saturate(dot(normalize(i.n), L));
    return float4(albedo * (0.2 + 0.8 * NdotL), 1);
}