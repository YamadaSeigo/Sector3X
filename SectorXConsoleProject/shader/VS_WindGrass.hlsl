#include "_GlobalTypes.hlsli"

cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gCellSizeXZ; // 1クラスタのサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    uint _pad00;
    uint _pad11;
};

cbuffer GrassWindCB : register(b11)
{
    float gTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gPhaseSpread; // ノイズからどれだけ位相をズラすか（ラジアン）
    float gBladeHeightLocal; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float2 gWindDirXZ; // XZ 平面の風向き (正規化済み)
};

Texture2D<float> gHeightMap : register(t10);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};


struct VSOutput
{
    float4 posH : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float2 uv : TEXCOORD3;
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
    float3 baseWorldPos = float3(world._m03, world._m13, world._m23);

    VSOutput output;
    float3 wp = mul(R, input.position) + baseWorldPos;

    float2 terrainSize = gCellSizeXZ * float2(gDimX, gDimZ);
    float2 heightMapUV = (baseWorldPos.xz - gOriginXZ) / terrainSize;
    float centerHeight = gHeightMap.SampleLevel(gSampler, heightMapUV, 0);
    float2 localUV = (wp.xz - gOriginXZ) / terrainSize;
    float localHeight = gHeightMap.SampleLevel(gSampler, localUV, 0);
    wp.y += (localHeight - centerHeight) * 70.0f;

   // ---- 1) 全体をまとめる“大きな揺れ” ----
   // 空間周波数をかなり低くして「大きなうねり」
    float bigSpatial = dot(baseWorldPos.xz, gWindDirXZ * 0.03f);

   // GrassMovementService 側でグルーブさせた Time を使う前提
    float bigPhase = bigSpatial + gTime * gWindSpeed;
    float bigWave = sin(bigPhase); // -1..1

   // ---- 2) 個々の“ゆらぎ”用の小さいノイズ ----
    float2 noisePos = baseWorldPos.xz * gNoiseFreq; // gNoiseFreq は 0.1 とか
    float noise01 = PseudoNoise2D(noisePos);
    float noiseN11 = noise01 * 2.0f - 1.0f;

   // 小さい振幅で “バラつき” だけを付ける
    float smallWave = noiseN11; // -1..1 のまま使うか、sin(noiseN11*定数)でもOK

   // ---- 3) 合成 ----
    float wave = bigWave * 0.7f + smallWave * 0.3f;

    // ---- 高さ割合（ローカルYでOK）----
    float heightFactor = 0.0f;
    if (gBladeHeightLocal > 0.0f)
    {
        float h = saturate(input.position.y / gBladeHeightLocal); // 0..1

        heightFactor = pow(h, 2.0f); // 下部ほど揺れにくい
    }

    float swayAmount = gWindAmplitude * centerHeight * wave * heightFactor;
    float3 windDir3 = float3(gWindDirXZ.x, 0.0f, gWindDirXZ.y);
    wp += windDir3 * swayAmount;

    output.posH = mul(uViewProj, float4(wp, 1.0f));
    output.worldPos = wp;
    output.normalWS = mul(R, input.normal); // 非一様スケール無し前提
    output.viewDepth = mul(uView, float4(wp, 1.0f)).z;
    output.uv = input.uv;

    return output;
}