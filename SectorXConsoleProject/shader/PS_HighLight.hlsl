struct PSInput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(0, 0.25f, 0, 1.0f);
}
