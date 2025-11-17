#include "_GlobalTypes.hlsli"

struct VSPosInput
{
    float3 position : LINE_POS; //固定のセマンティクスを使用しない(POSITION,COLORなど)
    uint rgba : LINE_COLOR;     //そうすることで固定の入力から外れてAoSで詰め込める
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float4 col : COLOR;
};

VSOutput main(VSPosInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    VSOutput output;
    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3)world;
    float3 t = float3(world._m03, world._m13, world._m23);

    const float3 wp = mul(R, input.position) + t;

    // クリップ座標
    output.clip = mul(uViewProj, float4(wp, 1.0));

    uint rgba = input.rgba;
    float4 c = float4(((rgba >> 24) & 0xFF) / 255.0, ((rgba >> 16) & 0xFF) / 255.0, ((rgba >> 8) & 0xFF) / 255.0, (rgba & 0xFF) / 255.0);
    output.col = c;

    return output;
}