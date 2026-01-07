cbuffer BloomCB : register(b0)
{
    float gBloomThreshold; // 例: 1.0（HDR前提） / LDRなら0.8等
    float gBloomKnee; // 例: 0.5（soft threshold）
    float gBloomIntensity; // 合成側で使ってもOK
    float _pad;
};

Texture2D gScene : register(t0);
Texture2D gBloom : register(t1);
SamplerState gLinearClamp : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 scene = gScene.Sample(gLinearClamp, uv).rgb;
    float3 bloom = gBloom.Sample(gLinearClamp, uv).rgb;

    return float4(scene + bloom * gBloomIntensity, 1);
}
