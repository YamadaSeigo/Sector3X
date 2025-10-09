#include "GlobalTypes.hlsli"

struct VSPosInput
{
    float3 position : LINE_POS; //固定のセマンティクスを使用しない(POSITION,COLORなど)
    uint rgba : LINE_COLOR; //そうすることで固定の入力から外れてAoSで詰め込める
};

struct VSOutput
{
    float4 posH : SV_POSITION;
    float4 col : COLOR;
};

VSOutput main(VSPosInput input, uint instId : SV_InstanceID)
{
    VSOutput output;
    output.posH = float4(input.position.x, input.position.y, 5.0f, input.position.z);

    uint rgba = input.rgba;
    float4 c = float4(((rgba >> 24) & 0xFF) / 255.0, ((rgba >> 16) & 0xFF) / 255.0, ((rgba >> 8) & 0xFF) / 255.0, (rgba & 0xFF) / 255.0);
    output.col = c;

    return output;
}