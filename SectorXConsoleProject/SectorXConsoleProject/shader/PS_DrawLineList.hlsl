#include "GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
