


cbuffer ViewProjectionBuffer : register(b0)
{
    row_major float4x4 uViewProj;
};

//==========================================
//ライトバッファここ
//==========================================

cbuffer MaterialCB : register(b2)
{
    float4 baseColorFactor; // glTF baseColorFactor
    float metallicFactor; // glTF metallicFactor
    float roughnessFactor; // glTF roughnessFactor
    float hasBaseColorTex; // 1 or 0
    float hasNormalTex; // 1 or 0
    float hasMRRTex; // 1 or 0 (MetallicRoughness)
    float3 _pad_; // 16B境界合わせ
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;

    float4 iRow0 : INSTANCE_ROW0;
    float4 iRow1 : INSTANCE_ROW1;
    float4 iRow2 : INSTANCE_ROW2;
    float4 iRow3 : INSTANCE_ROW3;
};


