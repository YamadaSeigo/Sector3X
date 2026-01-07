cbuffer BlurCB : register(b0)
{
    float2 gTexelSize; // 1/width, 1/height
    float2 _pad;
};

Texture2D gSrc : register(t0);
SamplerState gLinearClamp : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float2 o = float2(gTexelSize.x, 0);

    // 9tapÇ≠ÇÁÇ¢ÅièdÇØÇÍÇŒ5tapÇ÷Åj
    float3 sum =
        gSrc.Sample(gLinearClamp, uv - 4 * o).rgb * 0.016216 +
        gSrc.Sample(gLinearClamp, uv - 3 * o).rgb * 0.054054 +
        gSrc.Sample(gLinearClamp, uv - 2 * o).rgb * 0.121622 +
        gSrc.Sample(gLinearClamp, uv - 1 * o).rgb * 0.194594 +
        gSrc.Sample(gLinearClamp, uv).rgb * 0.227027 +
        gSrc.Sample(gLinearClamp, uv + 1 * o).rgb * 0.194594 +
        gSrc.Sample(gLinearClamp, uv + 2 * o).rgb * 0.121622 +
        gSrc.Sample(gLinearClamp, uv + 3 * o).rgb * 0.054054 +
        gSrc.Sample(gLinearClamp, uv + 4 * o).rgb * 0.016216;

    return float4(sum, 1);
}
