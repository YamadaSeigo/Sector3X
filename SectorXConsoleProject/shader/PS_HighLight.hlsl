struct PSInput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(0.8f, 0.8f, 0.8f, 1.0f);
}
