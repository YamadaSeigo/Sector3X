#include "GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float4 col : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col;
}
