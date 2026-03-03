cbuffer WetnessUpdateCB : register(b0)
{
    float gDt;
    float gDryRate; // 例: 0.05
    float gRainRate; // 例: 0.3
    float gGlobalWet; // 境界対策に使うなら
    uint2 gTexSize;
    float2 pad2;

    // --- ワールド対応（クリップマップ用） ---
    float2 gWetOriginXZ; // このWetnessRTの左下(または基準)のworld XZ
    float gWetWorldSize; // 1枚がカバーするワールド幅（正方形, meters）
    float gTimeSec; // 秒（継続的に増える）

    // --- 斑点（雨粒っぽいムラ）パラメータ ---
    float gSpeckleCellSize; // 例: 0.25 (m) 斑点の“セル”サイズ
    float gSpeckleDensity; // 例: 2.0  (大きいほど当たりやすい)
    float gSpeckleAmount; // 例: 0.15 (1回の当たりで足す濡れ量)
    float gSpeckleTimeHz; // 例: 10.0 (1秒に何回パターン更新するか)
};

// いったん遮蔽なし（後で RainOcclusionHeightMap を足す）
RWTexture2D<float> gWet : register(u0);

// ----------------- 安定乱数（セル＋時間量子化） -----------------
uint Hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}
float Rand01(uint v)
{
    return (Hash(v) & 0x00FFFFFF) / 16777216.0; // 0..1
}

// texel -> worldXZ（正方形前提）
float2 TexelToWorldXZ(uint2 xy)
{
    float2 uv = (float2(xy) + 0.5) / float2(gTexSize);
    return gWetOriginXZ + uv * gWetWorldSize;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x, y = tid.y;
    if (x >= gTexSize.x || y >= gTexSize.y)
        return;

    float wet = gWet[uint2(x, y)];

    // 乾燥（指数減衰の近似：exp(-k*dt)）
    wet *= exp(-gDryRate * gDt);

    // 降雨（遮蔽なし版）
    //wet += gRainRate * gDt;

    // ----------------- 斑点（雨粒っぽい当たり） -----------------
    // ワールド基準のセル+時間量子化で “チラつきにくい” 点々を作る
    float2 worldXZ = TexelToWorldXZ(uint2(x, y));

    float cellSize = max(0.0001, gSpeckleCellSize);
    int2 cell = (int2) floor(worldXZ / cellSize);

    // 時間を量子化（例：10Hz -> 0.1秒ごとにパターン更新）
    float hz = max(0.1, gSpeckleTimeHz);
    int tBucket = (int) floor(gTimeSec * hz);

    // cell + time で安定乱数
    uint key = (uint) (cell.x * 73856093) ^ (uint) (cell.y * 19349663) ^ (uint) (tBucket * 83492791);
    float r = Rand01(key);

    // 当たり確率 p：雨量(gRainRate)とdtと密度で決める
    // 目安：gRainRate=0.3, dt=0.016, density=2 → p ~ 0.0096
    float p = saturate(gRainRate * gDt * gSpeckleDensity);

    // 当たったら少しだけ濡れを足す
    float hit = step(1.0 - p, r);
    wet += hit * gSpeckleAmount;

    wet = saturate(wet);
    gWet[uint2(x, y)] = wet;
}