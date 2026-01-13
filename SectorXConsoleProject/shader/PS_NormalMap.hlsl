#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 posCS : SV_POSITION;
    float3 T_ws : TEXCOORD0;
    float3 B_ws : TEXCOORD1;
    float3 N_ws : TEXCOORD2;
    float2 uv : TEXCOORD4;
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
        if ((hasFlags & FLAG_HAS_OCCTEX) != 0u)
        {
            float occ = gOcclusionTex.Sample(gSampler, input.uv).r;
            occlution *= occ;
        }
    }

    output.AlbedoAO = float4(baseColor.rgb, occlution);

    //// 法線は面裏で反転
    //float3 N = isFrontFace ? normalize(input.normalWS) : -normalize(input.normalWS);

    float3 nTS = float3(0, 0, 1);
    if ((hasFlags & FLAG_HAS_NORMALTEX) != 0u)
    {
        float4 normalSample = gNormalTex.Sample(gSampler, input.uv);
        nTS = normalize(normalSample.xyz * 2.0f - 1.0f);
    }

     // TBN 行列を組む（列ベクトルか行ベクトルかはmul側に合わせる）
    float3x3 TBN = float3x3(
        normalize(input.T_ws),
        normalize(input.B_ws),
        normalize(input.N_ws)
    );

    // タンジェント -> ワールド
    float3 nWS = normalize(mul(nTS, TBN)); // nTS が行ベクトル想定

    output.NormalRoughness = float4(nWS * 0.5f + 0.5f, roughness);

    float4 emissionColor = float4(0, 0, 0, 0);
    if ((hasFlags & FLAG_HAS_EMISSIVETEX) != 0u)
        emissionColor = gEmissiveTex.Sample(gSampler, input.uv);

    output.EmissionMetallic = float4(emissionColor.rgb, metallic);

    return output;
}
