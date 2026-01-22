#include "_GlobalTypes.hlsli"

cbuffer WindCB : register(b11)
{
    float gTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float3 gWindDir; // XZ 平面の風向き (正規化済み)
};


struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 clip : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR0;
};

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    float3 wp = mul(R, input.position) + t;

    // ---- 根元固定用の重み (0..1) ----
    // ローカルYが 0=根元, height=先端 という想定
    float h = saturate(input.position.y + 0.5f); // 0..1
    float w = h * h * (3.0 - 2.0 * h); // smoothstep

    // ---- 風の位相：ワールド位置＋時間 ----
    float3 dir = normalize(gWindDir);
    float phaseBase = dot(t, dir) * gNoiseFreq;
    float phase = phaseBase + gTime * gWindSpeed;

    // ---- 揺れ量（基本波＋少し不規則）----
    float s1 = sin(phase);
    float s2 = sin(phase * 2.13 + 1.7); // 2本目でゆらぎ
    float gust = sin(phase * 0.37 + 5.0) * gWindAmplitude;

    float sway = (s1 * 0.7 + s2 * 0.3 + gust) * gWindAmplitude * 10.0f;

    // ---- オフセット方向（XZ）----
    float3 offsetW = float3(dir.x, 0.0, dir.y) * (sway * w);

    // 例：centerW を基準に「板をワールドに置いている」前提
    wp += offsetW;

    // クリップ座標
    output.clip = mul(uViewProj, float4(wp, 1.0));
    output.uv = input.uv;
    output.color = gInstanceMats[pooledIndex].color;
    return output;
}