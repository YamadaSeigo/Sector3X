#include "GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

Texture2D<float> gDepthTex : register(t5);

float4 main(PSInput input) : SV_TARGET
{    
    float z = gDepthTex.Sample(gSampler, input.uv); // z=1/w（線形ではない）
    float gray = saturate(z * 1.0f + 0.0f); // 調整用
    return float4(gray, gray, gray, 1);
}
