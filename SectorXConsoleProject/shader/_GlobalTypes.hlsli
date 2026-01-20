
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
    uint _pad0_PerDraw, _pad1_PerDraw;
};

cbuffer MaterialCB : register(b2)
{
    float4 baseColorFactor; // glTF baseColorFactor
    float metallicFactor;   // glTF metallicFactor
    float roughnessFactor;  // glTF roughnessFactor
    float occlutionFactor; // 16B 境界揃え
    uint hasFlags;          // フラグビット群
};

static const uint FLAG_HAS_BASECOLORTEX     = 1u << 0;
static const uint FLAG_HAS_NORMALTEX        = 1u << 1;
static const uint FLAG_HAS_MRTEX            = 1u << 2;
static const uint FLAG_HAS_OCCTEX           = 1u << 3;
static const uint FLAG_HAS_ORMCOMBIENT      = 1u << 4;
static const uint FLAG_HAS_EMISSIVETEX      = 1u << 5;


// カスケード情報（DX11ShadowMapService::CBShadowCascadesData と揃える）
cbuffer CBShadowCascades : register(b5)
{
    row_major float4x4 gLightViewProj[NUM_CASCADES]; // kMaxShadowCascades = 4

//***********************************************************************************************
    //NUM_CASCADESが増えたらループアンローラーが働かなくなるので注意(この場合最大4個まで)
//***********************************************************************************************
    float4 gCascadeSplits; // Camera の view-space 距離 (splitFar)


    uint gCascadeCount;
    float3 gCascadeDirection;
};


//==================================================================================
//コンスタントバッファのスロット5以降は、個別にセットしてもオケ!
//テクスチャバッファの場合は,t9以降はオケ!
//==================================================================================

// フレーム単位：全インスタンスのワールド行列（48B/個）
struct InstanceMat
{
    row_major float3x4 M;
    float4 color;
};
StructuredBuffer<InstanceMat> gInstanceMats : register(t0);

// ドロー単位：このメッシュで使うインスタンスの “参照インデックス” 群
StructuredBuffer<uint> gInstIndices : register(t1);

//AssetModelManagerのバインド名と合わせる
//==================================================================================
Texture2D gBaseColorTex : register(t2);

Texture2D gNormalTex : register(t3);

Texture2D gMetallicRoughness : register(t4);

Texture2D gOcclusionTex : register(t5);

Texture2D gEmissiveTex : register(t6);

//==================================================================================

// シャドウマップ (Texture2DArray)
Texture2DArray<float> gShadowMap : register(t7);


SamplerState gSampler : register(s0);

/*
===固定セマンティクス一覧=============================================================
   POSITION         : float3                = DXGI_FORMAT_R32G32B32_FLOAT
   NORMAL           : float3/float4         = DXGI_FORMAT_R8G8B8A8_SNORM
   TANGENT          : float3/float4         = DXGI_FORMAT_R8G8B8A8_SNORM
   TEXCOORD         : float2                = DXGI_FORMAT_R16G16_FLOAT
   BLENDINDICES     : uint4/uint2/uint      = DXGI_FORMAT_R8G8B8A8_UINT
   BLENDWEIGHT      : float4/float2/float   = DXGI_FORMAT_R8G8B8A8_UNORM
   COLOR            : float3/float4         = DXGI_FORMAT_R8G8B8A8_UNORM
==================================================================================
*/

struct PS_PRBOutput
{
    float4 AlbedoAO : SV_Target0; // RGB=Albedo, A=AO
    float4 NormalRoughness : SV_Target1; // RGB=Normal, A=Roughness
    float4 EmissionMetallic : SV_Target2; // RGB=Emission, A=Metallic
};