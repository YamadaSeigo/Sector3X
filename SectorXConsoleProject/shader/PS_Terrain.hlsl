
struct PSIN
{
    float4 clip : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSIN pin) : SV_TARGET
{
    return float4(1, 1, 1, 1);
}