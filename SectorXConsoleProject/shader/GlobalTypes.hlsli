
cbuffer ViewProjectionBuffer : register(b0)
{
    row_major float4x4 uViewProj;
};

cbuffer PerDraw : register(b1)
{
    uint gIndexBase; // このドローのインデックス列の先頭オフセット
    uint gIndexCount; // このドローのインスタンス数（任意、PS側などで使うなら）
    uint _pad0, _pad1;
};

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

cbuffer LightCB : register(b3)
{
    float4 lightDir; // ワールド空間、正規化済み
    float4 lightColor; // 0～1
    float4 ambientColor; // 環境光
};

//======================================================
//コンスタントバッファのスロット４以降は、個別にセットしてもオケ!
//======================================================

// フレーム単位：全インスタンスのワールド行列（64B/個）
StructuredBuffer<float4x4> gInstanceMats : register(t0);

// ドロー単位：このメッシュで使うインスタンスの “参照インデックス” 群
StructuredBuffer<uint> gInstIndices : register(t1);

Texture2D gBaseColorTex : register(t2);

Texture2D gNormalTex : register(t3);

Texture2D gMetallicRoughness : register(t4);

SamplerState gSampler : register(s0);

/*
===固定セマンティクス一覧=========================================
   POSITION         : float3/float4
   NORMAL           : float3
   TANGENT          : float3/float4
   TEXCOORD         : float2
   BLENDINDICES     : uint4/uint2/uint
   BLENDWEIGHT      : float4/float2/float
   COLOR            : float3/float4
============================================================
*/

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};
