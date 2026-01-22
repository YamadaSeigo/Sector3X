#include "_GlobalTypes.hlsli"

// 地形グリッド情報
cbuffer TerrainGridCB : register(b10)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z) 
    float2 gClusterXZ; // 1クラスタのワールドサイズ (x,z) ※同上
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    float offsetY;

    // Heightfield 全体の頂点数
    uint gVertsX; // (= vertsX)
    uint gVertsZ; // (= vertsZ)

    uint2 padding; // 未使用
    
    float2 gCellSize; // Heightfield のセルサイズ (x,z)
    float2 gHeightMapInvSize; // 1/width, 1/height
};

cbuffer WindCB : register(b11)
{
    float gTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float3 gWindDir; // XZ 平面の風向き (正規化済み)
};


cbuffer GrassFootCB : register(b12)
{
    // 最大何個まで踏んでいる領域を考慮するか
    static const int MAX_FOOT = 4;
    float4 gFootPosWRadiusWS[MAX_FOOT]; // ワールド座標 (足元 or カプセル中心付近)
    float gFootStrength; // 全体の曲がり強さ
    float gHeightRange;
    int gFootCount; // 有効な足の数
    float _pad;
};


struct VSInput
{
    float3 position : POSITION;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};


struct VSOutput
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
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
    
   // ---- 1) 全体をまとめる“大きな揺れ” ----
   // 空間周波数をかなり低くして「大きなうねり」
    float bigSpatial = dot(baseWorldPos, gWindDir * 0.03f);

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

    float weight = saturate(input.normal.w);

     // --------- ここから「踏まれオフセット」 ----------
    // 事前
    float3 footOffset = 0.0f;
    float tipWeight = input.normal.w;

    if (gFootCount > 0)
    {
        const float invHeightRange = rcp(gHeightRange);
        const float eps = 1e-8f;

        // MAX_FOOT は 4 なので固定 unroll が強い
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            if (i >= gFootCount)
                break;

            float3 footPos = gFootPosWRadiusWS[i].xyz;
            float radius = gFootPosWRadiusWS[i].w;

            float2 dXZ = baseWorldPos.xz - footPos.xz;
            float dist2 = dot(dXZ, dXZ);
            float r2 = radius * radius;

            // ほとんどが範囲外なら分岐はむしろ有利
            if (dist2 < r2)
            {
                float invR = rcp(radius);

                // dist と dirXZ を rsqrt で作る（sqrt を避ける）
                float invDist = rsqrt(max(dist2, eps));
                float dist = dist2 * invDist; // = sqrt(dist2)
                float2 dirXZ = dXZ * invDist; // 正規化

                // t = 1 - dist/radius
                float t = 1.0f - dist * invR;
                t *= t;

                float dy = wp.y - footPos.y;
                float hFactor = saturate(1.0f - abs(dy) * invHeightRange);

                float bend = gFootStrength * t * hFactor * tipWeight;

                footOffset.xz += dirXZ * bend;
                footOffset.y -= bend * 0.5f;
            }
        }
    }
    wp += footOffset;

    // --------- ここまで踏まれ処理 ----------

    float swayAmount = gWindAmplitude * wave * weight;
    wp += gWindDir * swayAmount;

    output.posH = mul(uViewProj, float4(wp, 1.0f));
    output.normalWS = mul(R, input.normal.xyz); // 非一様スケール無し前提
    output.uv = input.uv;

    return output;
}