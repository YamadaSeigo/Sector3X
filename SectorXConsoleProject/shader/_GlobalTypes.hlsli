
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

float DebugShadowDepth(float3 worldPos, uint cascade)
{
    float4 shadowPos = mul(gLightViewProj[cascade], float4(worldPos, 1.0f));
    shadowPos.xyz /= shadowPos.w;

    float2 uv = shadowPos.xy * 0.5f + 0.5f;

    if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1)
        return 1.0f;

    uv.y = 1.0f - uv.y;

    float z = shadowPos.z;

    // uv, z を 0..1 にクランプ（とりあえず範囲外チェックは外す）
    uv = saturate(uv);
    z = saturate(z);

    // 深度バッファの中身をそのまま読む
    float depthTex = gShadowMap.SampleLevel(
        gSampler,
        float3(uv, cascade),
        0
    ).r;

    return depthTex; // これをそのまま色として返してみる
}

float SampleShadow(float3 worldPos, float viewDepth)
{
    // どのカスケードを使うか
    uint cascade = ChooseCascade(viewDepth);

    // 対応するライトVPでライト空間へ変換
    float4 shadowPos = mul(gLightViewProj[cascade], float4(worldPos, 1.0f));

    // NDC に正規化
    shadowPos.xyz /= shadowPos.w;

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
    const float depthBias = 0.1f;

    // PCF のサンプル範囲
    // （シャドウマップの解像度は C++ 側から逆数を渡してもよい）
    const float2 texelSize = 1.0f / float2(960.0f, 720.0f);
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

// viewDepth: view-space Z
// returns: baseIdx, nextIdx, blendFactor
void EvaluateCascade(float viewDepth,
                     out uint baseIdx,
                     out uint nextIdx,
                     out float blend)
{
    // gCascadeSplits: splitFar[0..N-1]
    baseIdx = 0;
    [unroll]
    for (uint i = 0; i < gCascadeCount - 1; ++i)
    {
        if (viewDepth > gCascadeSplits[i])
            baseIdx = i + 1;
    }

    nextIdx = min(baseIdx + 1u, gCascadeCount - 1u);

    // ブレンド開始距離（view-space）
    const float blendRange = 5.0f; // 調整ポイント（単位は view-space Z）

    if (baseIdx == gCascadeCount - 1)
    {
        blend = 0.0f;
        return;
    }

    float splitNear = gCascadeSplits[baseIdx];
    float splitFar = gCascadeSplits[nextIdx];

    // baseIdx の far 付近でブレンド
    float start = splitFar - blendRange;
    float t = saturate((viewDepth - start) / blendRange);

    blend = t; // 0 → base, 1 → next
}

float SampleShadowCascade(float3 worldPos, uint cascade)
{
    float4 shadowPos = mul(float4(worldPos, 1.0f), gLightViewProj[cascade]);
    shadowPos.xyz /= shadowPos.w;

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
    const float depthBias = 0.0005f;
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
    uint idx0, idx1;
    float blend;
    EvaluateCascade(viewDepth, idx0, idx1, blend);

    float s0 = SampleShadowCascade(worldPos, idx0);
    float s1 = SampleShadowCascade(worldPos, idx1);

    return lerp(s0, s1, blend);
}
