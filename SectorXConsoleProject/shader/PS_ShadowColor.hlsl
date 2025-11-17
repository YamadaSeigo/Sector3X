#include "_GlobalTypes.hlsli"
#include "_ShadowTypes.hlsli"

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

//==================================================
// ヘルパー：シャドウマップサンプル (PCF 付き)
//==================================================

float SampleShadow(float3 worldPos, float viewDepth)
{
    // どのカスケードを使うか
    uint cascade = ChooseCascade(viewDepth);

    // 対応するライトVPでライト空間へ変換
    float4 shadowPos = mul(gLightViewProj[cascade] ,float4(worldPos, 1.0f));

    // NDC に正規化
    shadowPos.xyz /= shadowPos.w;

    // LH + ZeroToOne の正射影を使っている前提:
    // x,y: -1..1, z: 0..1 になるよう Projection を作っておく。
    float2 uv = shadowPos.xy * 0.5f + 0.5f;
    float z = shadowPos.z;

    return z;

    // カスケード外なら影なし
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        z < 0.0f || z > 1.0f)
    {
        return 1.0f; // 完全にライトが当たっている扱い
    }

    // 深度バイアス（アーティファクトを見ながら調整）
    const float depthBias = 0.0005f;

    // PCF のサンプル範囲
    // （シャドウマップの解像度は C++ 側から逆数を渡してもよい）
    const float2 texelSize = 1.0f / float2(2048.0f, 2048.0f);
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

struct PSInput
{
    float4 posH : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float viewDepth : TEXCOORD2; // ← これを PS で使う
    float2 uv : TEXCOORD3;
};

float4 main(PSInput input) : SV_TARGET
{
    // カスケードシャドウのサンプル
    float shadow = SampleShadow(input.worldPos, input.viewDepth);

    return float4(shadow, 0, 0, 1);

    if (hasBaseColorTex == 0)
        return baseColorFactor * shadow;

    float4 baseColor = gBaseColorTex.Sample(gSampler, input.uv);
    return baseColor * baseColorFactor * shadow;
}
