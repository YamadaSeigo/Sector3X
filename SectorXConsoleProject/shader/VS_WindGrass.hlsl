#include "_GlobalTypes.hlsli"

cbuffer GrassWindCB : register(b11)
{
    float gTime; // 経過時間
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float2 gWindDirXZ; // XZ 平面の風向き (正規化済み)
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gPhaseSpread; // ノイズからどれだけ位相をズラすか（ラジアン）
    float gBladeHeightLocal; // 1本あたりの高さ（ローカルY の最大値）
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float PseudoNoise2D(float2 p)
{
    const float2 K = float2(127.1, 311.7);
    float h = dot(p, K);
    return frac(sin(h) * 43758.5453123);
}

VSOutput main(VSInput input, uint instId : SV_InstanceID)
{
    uint pooledIndex = gInstIndices[gIndexBase + instId]; //間接参照

    row_major float3x4 world = gInstanceMats[pooledIndex].M;

    float3x3 R = (float3x3) world;
    float3 t = float3(world._m03, world._m13, world._m23);

    // 行列から平行移動成分を取り出す
    float3 baseWorldPos = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    float3 wp = mul(R, input.position) + t;

     // ---- 疑似ノイズ（インスタンス単位） ----
    float2 noisePos = baseWorldPos.xz * gNoiseFreq;
    float noise01 = PseudoNoise2D(noisePos);
    float noiseN11 = noise01 * 2.0f - 1.0f;
    float phaseOffset = noiseN11 * gPhaseSpread;

    // ---- 位相 ----
    float spatialTerm = dot(baseWorldPos.xz, gWindDirXZ);
    float phase = spatialTerm + gTime * gWindSpeed + phaseOffset;
    float wave = sin(phase);

    // ---- 高さ割合（ローカルYでOK）----
    float heightFactor = 0.0f;
    if (gBladeHeightLocal > 0.0f)
    {
        heightFactor = saturate(input.position.y / gBladeHeightLocal);
    }

    float swayAmount = gWindAmplitude * wave * heightFactor;
    float3 windDir3 = float3(gWindDirXZ.x, 0.0f, gWindDirXZ.y);
    wp += windDir3 * swayAmount;

    output.posH = mul(float4(wp, 1.0f), uViewProj);
    output.normal = mul(R, input.normal); // 非一様スケール無し前提
    output.uv = input.uv;

    return output;
}