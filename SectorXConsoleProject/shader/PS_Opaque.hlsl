#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normalWS : TEXCOORD1;
};

PS_PRBOutput main(PSInput input, bool isFrontFace : SV_IsFrontFace)
{
    PS_PRBOutput output;

    float4 baseColor = baseColorFactor;
    if ((hasFlags & FLAG_HAS_BASECOLORTEX) != 0u)
        baseColor *= gBaseColorTex.Sample(gSampler, input.uv);

    float occlution = occlutionFactor;
    float roughness = roughnessFactor;
    float metallic = metallicFactor;

    if ((hasFlags & FLAG_HAS_ORMCOMBIENT) != 0u)
    {
        float4 orm = gMetallicRoughness.Sample(gSampler, input.uv);
        occlution *= orm.r;
        roughness *= orm.g;
        metallic *= orm.b;
    }
    else
    {
        if ((hasFlags & FLAG_HAS_MRTEX) != 0u)
        {
            float4 mr = gMetallicRoughness.Sample(gSampler, input.uv);
            roughness *= mr.g;
            metallic *= mr.b;
        }
        if((hasFlags & FLAG_HAS_OCCTEX) != 0u)
        {
            float occ = gOcclusionTex.Sample(gSampler, input.uv).r;
            occlution *= occ;
        }
    }

    output.AlbedoAO = float4(baseColor.rgb, occlution);

    // ñ@ê¸ÇÕñ ó†Ç≈îΩì]
    float3 N = isFrontFace ? normalize(input.normalWS) : -normalize(input.normalWS);

    output.NormalRoughness = float4(N * 0.5f + 0.5f, roughness);

    float4 emissionColor = float4(0, 0, 0, 0);
    if ((hasFlags & FLAG_HAS_EMISSIVETEX) != 0u)
        emissionColor = gEmissiveTex.Sample(gSampler, input.uv);

    output.EmissionMetallic = float4(emissionColor.rgb, metallic);

    return output;
}
