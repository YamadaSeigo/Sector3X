
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


// カスケード情報（DX11ShadowMapService::CBShadowCascadesData と揃える）
cbuffer CBShadowCascades : register(b5)
{
    row_major float4x4 gLightViewProj[NUM_CASCADES]; // kMaxShadowCascades = 4

//***********************************************************************************************
    //NUM_CASCADESが増えたらループアンローラーが働かなくなるので注意
//***********************************************************************************************
    float4 gCascadeSplits; // Camera の view-space 距離 (splitFar)


    uint gCascadeCount;
    float3 gCascadeDirection;
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

//==================================================
// ヘルパー：カスケード選択
//==================================================

uint ChooseCascade(float viewDepth)
{
    // viewDepth はカメラ view-space の Z（LHなら +Z が前）、
    // gCascadeSplits[i] には LightShadowService の splitFar[i] を入れる想定

    uint idx = 0;

    // gCascadeCount-1 まで比較し、閾値を超えたら次のカスケードへ
    [unroll]
    for (uint i = 0; i < NUM_CASCADES - 1; ++i)
    {
        if (i < gCascadeCount - 1)
        {
            // viewDepth が split を超えたらインデックスを進める
            idx += (viewDepth > gCascadeSplits[i]) ? 1u : 0u;
        }
    }

    return min(idx, gCascadeCount - 1);
}

float GetShadowMapDepth(float3 shadowPos, uint cascade)
{
    float2 uv = shadowPos.xy * 0.5f + 0.5f;

    if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1)
        return 1.0f;

    uv.y = 1.0f - uv.y;

    // uv, z を 0..1 にクランプ（とりあえず範囲外チェックは外す）
    uv = saturate(uv);

    // 深度バッファの中身をそのまま読む
    float depthTex = gShadowMap.Sample(
        gSampler,
        float3(uv, cascade)
    ).r;

    return depthTex; // これをそのまま色として返してみる
}

float SampleShadow(float3 worldPos, float viewDepth)
{
    // どのカスケードを使うか
    uint cascade = ChooseCascade(viewDepth);

    // 対応するライトVPでライト空間へ変換
    float4 shadowPos = mul(gLightViewProj[cascade], float4(worldPos, 1.0f));

    // LH + ZeroToOne の正射影を使っている前提:
    // x,y: -1..1, z: 0..1 になるよう Projection を作っておく。
    float2 uv = shadowPos.xy * 0.5f + 0.5f;
    float z = shadowPos.z;

    // カスケード外なら影なし
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        z < 0.0f || z > 1.0f)
    {
        return 1.0f; // 完全にライトが当たっている扱い
    }

    uv.y = 1.0f - uv.y; // テクスチャ座標系に変換

    // 深度バイアス（アーティファクトを見ながら調整）
    const float depthBias = 0.1f * (cascade);

    // PCF のサンプル範囲
    // （シャドウマップの解像度は C++ 側から逆数を渡してもよい）
    const float2 texelSize = 1.0f / float2(1024.0f, 1024.0f * 6);
    const int kernelRadius = 1; // 3x3 PCF

    float shadow = 0.0f;
    int count = 0;

    // Comparison Sampler を使った 3x3 PCF
    [unroll]
    for (int dy = -kernelRadius; dy <= kernelRadius; ++dy)
    {
        [unroll]
        for (int dx = -kernelRadius; dx <= kernelRadius; ++dx)
        {
            float2 offset = float2(dx, dy) * texelSize;

            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                float3(uv + offset, cascade),
                z - depthBias
            );
            ++count;
        }
    }

    shadow /= max(count, 1);

    return shadow; // 1 = 影なし, 0 = 完全に影
}

struct CascadeInfo
{
    uint idx0;
    uint idx1;
    float t; // 0..1 でブレンド
};

CascadeInfo ChooseCascadeBlend(float viewDepth)
{
    CascadeInfo r;

    // どのカスケードに属するか探す
    uint c = 0;
    [unroll]
    for (uint i = 0; i < gCascadeCount; ++i)
    {
        if (viewDepth < gCascadeSplits[i])
        {
            c = i;
            break;
        }
    }

    // 手前側
    uint c0 = c > 0 ? c - 1 : 0;
    // 奥側
    uint c1 = c;

    float d0 = gCascadeSplits[c0];
    float d1 = gCascadeSplits[c1];

    float t = saturate((viewDepth - d0) / (d1 - d0));

    r.idx0 = c0;
    r.idx1 = c1;
    r.t = t;
    return r;
}
float SampleShadowCascade(float3 worldPos, uint cascade)
{
    float4 shadowPos = mul(float4(worldPos, 1.0f), gLightViewProj[cascade]);

    float2 uv = shadowPos.xy * 0.5f + 0.5f;
    float z = shadowPos.z;

    uv.y = 1.0f - uv.y;

    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        z < 0.0f || z > 1.0f)
    {
        return 1.0f;
    }

    const float2 texelSize = 1.0f / float2(960.0f, 720.0f);

    const float depthBias = 0.0f;
    const int kernelRadius = 1;

    float shadow = 0.0f;
    int count = 0;

    [unroll]
    for (int dy = -kernelRadius; dy <= kernelRadius; ++dy)
    {
        [unroll]
        for (int dx = -kernelRadius; dx <= kernelRadius; ++dx)
        {
            float2 offset = float2(dx, dy) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                float3(uv + offset, cascade),
                z - depthBias
            );
            ++count;
        }
    }

    return shadow / max(count, 1);
}

float SampleShadowLerp(float3 worldPos, float viewDepth)
{
    CascadeInfo ci = ChooseCascadeBlend(viewDepth);

    float sh0 = SampleShadowCascade(worldPos, ci.idx0);
    float sh1 = SampleShadowCascade(worldPos, ci.idx1);

    return lerp(sh0, sh1, ci.t);
}
