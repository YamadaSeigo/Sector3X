
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

struct SpriteInfo
{
    uint2 div2;
    uint2 frameIdx;
};

StructuredBuffer<SpriteInfo> gSpriteInfos : register(t11);

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    const float3 wp = mul(R, input.position) + t;

    // クリップ座標
    output.clip = mul(uViewProj, float4(wp, 1.0));

    SpriteInfo spriteInfo = gSpriteInfos[pooledIndex];

    // UV調整
    float2 cellSize = float2(1.0f, 1.0f) / spriteInfo.div2;
    float2 uv = spriteInfo.frameIdx * cellSize + input.uv * cellSize;

    output.uv = uv;

    return output;
}