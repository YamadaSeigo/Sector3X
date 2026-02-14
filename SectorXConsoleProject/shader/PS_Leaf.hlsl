#include "_GlobalTypes.hlsli"

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
    float3 nrmWS : TEXCOORD1;
};

Texture2D gLeafTex : register(t0);
SamplerState gLeafSamp : register(s0);

PS_PRBOutput main(VSOut i)
{
    PS_PRBOutput output;

    float4 texCol = gLeafTex.Sample(gLeafSamp, i.uv);

    // アルファクリッピング
    const float cutoff = 0.5f;
    clip(texCol.a - cutoff);

    float3 leafCol = i.col * texCol.rgb;

    output.AlbedoAO = float4(leafCol, 1);
    output.EmissionMetallic = float4(0, 0, 0, 1);

    float3 n = normalize(i.nrmWS);
    output.NormalRoughness = float4(n * 0.5 + 0.5, 1.0); // roughness=1.0 例

    return output;
}
