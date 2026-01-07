#include "_GlobalTypes.hlsli"

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : TEXCOORD1;
    float a : TEXCOORD2;
};

PS_PRBOutput main(VSOut i)
{
    PS_PRBOutput output;

    // è_ÇÁÇ©Ç¢ä€Åiä»à’Åj
    float2 p = i.uv * 2.0f - 1.0f;
    float r2 = dot(p, p);
    float falloff = saturate(1.0f - r2);
    falloff *= falloff;

    float3 emissive = i.col * (i.a * falloff);

    output.AlbedoAO = float4(0, 0, 0, 1);
    output.EmissionMetallic = float4(emissive, 1);
    output.NormalRoughness = float4(0, 0, 0, 1);

    return output;
}
