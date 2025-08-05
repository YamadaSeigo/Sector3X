cbuffer MaterialParams : register(b2)
{
    float4 baseColorFactor;
};

Texture2D baseColorTexture : register(t0);
SamplerState baseColorSampler : register(s0);

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
    
    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.uv);
    return baseColor * baseColorFactor;
}
