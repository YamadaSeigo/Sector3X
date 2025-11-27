#include "_GlobalTypes.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    //平行移動成分除去
    float3x3 R = (float3x3)uView;

    VSOutput output;
    const float3 wp = mul(R, input.position);
    float3 nW = mul(R, input.normal); // 非一様スケール無し前提

    // クリップ座標
    float4 clip = mul(uProj, float4(wp, 1.0));
    clip.z = clip.w; //Zバッファ最遠値に設定
    output.clip = clip;
    output.uv = input.uv;
    output.normal = nW;
    return output;
}