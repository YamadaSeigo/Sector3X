#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    //平行移動成分除去
    float3x3 R = (float3x3)uView;

    VSOutput output;
    const float3 wp = mul(R, input.position);

    // クリップ座標
    float4 clip = mul(uProj, float4(wp, 1.0));
    clip.z = clip.w; //Zバッファ最遠値に設定
    output.clip = clip;
    output.uv = input.uv;
    return output;
}