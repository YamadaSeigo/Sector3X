
#include "_ShadowTypes.hlsli"

cbuffer ViewProjectionBuffer : register(b0)
{
    row_major float4x4 uView;
    row_major float4x4 uProj;
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

cbuffer LightingCB : register(b3)
{
    // Sun / directional
    float3 gSunDirectionWS;
    float gSunIntensity; // 16B
    float3 gSunColor;
    float gAmbientIntensity; // 16B

    // Ambient + counts
    float3 gAmbientColor;
    uint gPointLightCount; // 16B
};

#define NUM_CASCADES 4

// カスケード情報（DX11ShadowMapService::CBShadowCascadesData と揃える）
cbuffer CBShadowCascades : register(b5)
{
    row_major float4x4 gLightViewProj[NUM_CASCADES]; // kMaxShadowCascades = 4

//***********************************************************************************************
    //NUM_CASCADESが増えたらループアンローラーが働かなくなるので注意
//***********************************************************************************************
    float4 gCascadeSplits; // Camera の view-space 距離 (splitFar)


    uint gCascadeCount;
    float3 _pad;
};


//======================================================
//コンスタントバッファのスロット5以降は、個別にセットしてもオケ!
//テクスチャバッファの場合は,t7以降はオケ!
//======================================================

// フレーム単位：全インスタンスのワールド行列（48B/個）
struct InstanceMat
{
    row_major float3x4 M;
};
StructuredBuffer<InstanceMat> gInstanceMats : register(t0);

// ドロー単位：このメッシュで使うインスタンスの “参照インデックス” 群
StructuredBuffer<uint> gInstIndices : register(t1);

Texture2D gBaseColorTex : register(t2);

Texture2D gNormalTex : register(t3);

Texture2D gMetallicRoughness : register(t4);

struct PointLight
{
    float3 positionWS;
    float radius; // 16B
    float3 color;
    float invRadius; // 16B
    int shadowLayer;
    float shadowBias; // 8B
    float2 _padPL0; // 8B (16B 境界揃え)
};

StructuredBuffer<PointLight> gPointLights : register(t5);

// シャドウマップ (Texture2DArray)
Texture2DArray<float> gShadowMap : register(t7);

SamplerState gSampler : register(s0);

// 比較サンプラ（ShadowMapService が作っているもの）
SamplerComparisonState gShadowSampler : register(s1);

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