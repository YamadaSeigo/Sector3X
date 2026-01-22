#include "_GlobalTypes.hlsli"

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
};

Texture2D gLeafTex : register(t0);
SamplerState gLeafSamp : register(s0);

PS_PRBOutput main(VSOut i)
{
    PS_PRBOutput output;
    
    float3 texCol = gLeafTex.Sample(gLeafSamp, i.uv).rgb;
    float3 leafCol = i.col * texCol;

    output.AlbedoAO = float4(leafCol, 1);
    output.EmissionMetallic = float4(0, 0, 0, 1);
    output.NormalRoughness = float4(0, 0, 0, 1);

    return output;
}
