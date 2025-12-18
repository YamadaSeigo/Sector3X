
Texture2D gAlbedoAO : register(t11);
Texture2D gNormalRough : register(t12);
Texture2D gMetalEmi : register(t13);
Texture2D gDepth : register(t14); // depth SRV‰»‚µ‚Ä‚¢‚é‘z’è
SamplerState gSamp : register(s0);

cbuffer CameraBuffer : register(b11)
{
    row_major float4x4 invViewProj;
    float4 camForward; //w‚Ípadding
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

    float4 albedoAO = gAlbedoAO.Sample(gSamp, uv);
    float4 nr = gNormalRough.Sample(gSamp, uv);
    float4 me = gMetalEmi.Sample(gSamp, uv);

    // ‚±‚±‚Å depth ‚©‚ç worldPos •œŒ³ + PBR lighting
    // ...
    float3 color = albedoAO.rgb; // ‰¼

    return float4(color, 1.0);
}
