cbuffer BloomCB : register(b0)
{
    float gBloomThreshold; // 例: 1.0（HDR前提） / LDRなら0.8等
    float gBloomKnee; // 例: 0.5（soft threshold）
    float gBloomIntensity; // 合成側で使ってもOK
    float _pad;
};

Texture2D gSceneColor : register(t0);
SamplerState gLinearClamp : register(s0);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// “soft threshold”
// https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/ みたいな形の定番
float SoftThreshold(float luma, float threshold, float knee)
{
    float t = luma - threshold;
    float soft = saturate(t / max(knee, 1e-5f));
    // knee内はなめらかに立ち上げ
    return soft * soft * (3.0f - 2.0f * soft);
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = gSceneColor.Sample(gLinearClamp, uv).rgb;

    float l = Luminance(c);
    float w = SoftThreshold(l, gBloomThreshold, gBloomKnee);

    // 閾値以上成分だけ残す（softに残る）
    return float4(c * w, 1);
}
