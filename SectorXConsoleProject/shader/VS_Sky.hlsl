#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float3 dir : TEXCOORD;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    float3x3 R = (float3x3) uView;

    VSOutput output;
    float3 wp = mul(R, input.position);

    float4 clip = mul(uProj, float4(wp, 1.0));
    clip.z = clip.w;
    output.clip = clip;

    output.dir = normalize(input.position);
    return output;
}