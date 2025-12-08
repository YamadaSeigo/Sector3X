#include "_GlobalTypes.hlsli"

/**
草を揺らす処理と地形の高さに一致させる処理を同じシェーダーで行っている
**/

cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gCellSizeXZ; // 1クラスタのサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    uint _pad00;
    uint _pad11;
};

cbuffer WindCB : register(b11)
{
    float gTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gPhaseSpread; // ノイズからどれだけ位相をズラすか（ラジアン）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float2 gWindDirXZ; // XZ 平面の風向き (正規化済み)
};

cbuffer GrassFootCB : register(b12)
{
    // 最大何個まで踏んでいる領域を考慮するか
    static const int MAX_FOOT = 4;
    float4 gFootPosWRadiusWS[MAX_FOOT]; // ワールド座標 (足元 or カプセル中心付近)
    float gFootStrength; // 全体の曲がり強さ
    int gFootCount; // 有効な足の数
    float2 _pad;
};

Texture2D<float> gHeightMap : register(t10);

struct VSInput
{
    float3 position : POSITION;
    float4 normal : NORMAL; //wに揺れの重さをいれる
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

// かなり軽い 2D ハッシュ（整数演算ベース）
float Hash2D(float2 p)
{
    // 適当な整数に変換
    uint2 n = uint2(floor(p)) * uint2(374761393u, 668265263u);
    uint h = n.x ^ n.y;

    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;

    // 0..1 に正規化 (1/2^32)
    return h * (1.0 / 4294967296.0);
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
    float2 cHeightMapUV = (baseWorldPos.xz - gOriginXZ) / terrainSize;
    float terrainHeight = gHeightMap.SampleLevel(gSampler, cHeightMapUV, 0);
    float2 lHeightMapUV = (wp.xz - gOriginXZ) / terrainSize;
    float localHeight = gHeightMap.SampleLevel(gSampler, lHeightMapUV, 0);
    wp.y += (localHeight - terrainHeight) * 70.0f;

   // ---- 1) 全体をまとめる“大きな揺れ” ----
   // 空間周波数をかなり低くして「大きなうねり」
    float bigSpatial = dot(baseWorldPos.xz, gWindDirXZ * 0.03f);

   // GrassMovementService 側でグルーブさせた Time を使う前提
    float bigPhase = bigSpatial + gTime * gWindSpeed;
    float bigWave = sin(bigPhase); // -1..1

   // ---- 2) 個々の“ゆらぎ”用の小さいノイズ ----
    float2 noisePos = baseWorldPos.xz * gNoiseFreq; // gNoiseFreq は 0.1 とか
    float noise01 = Hash2D(noisePos);
    float noiseN11 = noise01 * 2.0f - 1.0f;

   // 小さい振幅で “バラつき” だけを付ける
    float smallPhase = noiseN11 + gTime * gWindSpeed;
    float smallWave = sin(smallPhase);

   // ---- 3) 合成 ----
    float wave = bigWave * gBigWaveWeight + smallWave * (1.0f - gBigWaveWeight);

    float weight = input.normal.w;

    // --------- ここから「踏まれオフセット」 ----------
    float3 footOffset = 0.0f;

    // 草の上の方ほどよく倒れるようにするウェイト（既存と同じ）
    float tipWeight = input.normal.w;

    [unroll]
    for (int i = 0; i < gFootCount; ++i)
    {
        float3 footPos = gFootPosWRadiusWS[i].xyz;
        float radius = gFootPosWRadiusWS[i].w;

        // XZ 平面上の距離
        float2 dXZ = wp.xz - footPos.xz;
        float dist = length(dXZ);

        if (dist < radius)
        {
            // 半径内で 0～1 の減衰
            float t = 1.0f - dist / radius;
            // 少し滑らかに（内側ほど強く）したいので二乗
            t *= t;

            // 高さ方向での制限（足よりだいぶ上はあまり動かさない）
            // footPos.y を地面 or 足裏の高さとして扱う想定
            float heightRange = 2.5f; // 50cm くらいまで強く影響
            float dy = wp.y - footPos.y;
            float hFactor = saturate(1.0f - abs(dy) / heightRange);

            // どの方向に倒すか：足の中心から外側に逃がすイメージ
            float2 dirXZ = (dist > 1e-3f) ? (dXZ / dist) : float2(0.0f, 0.0f);

            float bend = gFootStrength * t * hFactor * tipWeight;

            // XZ 方向に押し倒す
            footOffset.xz += dirXZ * bend;

            // 少しだけ下方向にも沈めると「踏みつぶされた感」が出る
            footOffset.y -= bend * 0.5f;
        }
    }

    // 踏まれオフセットを適用
    wp += footOffset;
    // --------- ここまで踏まれ処理 ----------

    float swayAmount = gWindAmplitude * terrainHeight * wave * weight;
    float3 windDir3 = float3(gWindDirXZ.x, 0.0f, gWindDirXZ.y);
    wp += windDir3 * swayAmount;

    output.posH = mul(uViewProj, float4(wp, 1.0f));
    output.worldPos = wp;
    output.normalWS = mul(R, input.normal.xyz); // 非一様スケール無し前提
    output.viewDepth = mul(uView, float4(wp, 1.0f)).z;
    output.uv = input.uv;

    return output;
}