// CS_LeafUpdate.hlsl

#include "_LeafParticles.hlsli"

StructuredBuffer<LeafVolumeGPU> gVolumes : register(t0);
StructuredBuffer<uint> gAliveIn : register(t1);
ByteAddressBuffer gAliveCountRaw : register(t2);

Texture2D<float> gHeightMap : register(t3);
SamplerState gHeightSamp : register(s0);

// particles
RWStructuredBuffer<LeafParticle> gParticles : register(u0);
// AliveOut は append UAV
AppendStructuredBuffer<uint> gAliveOut : register(u1);

// FreeList は append UAV（返却用）
AppendStructuredBuffer<uint> gFreeList : register(u2);

// slot別の数
RWStructuredBuffer<uint> gVolumeCount : register(u3);


cbuffer CBUpdate : register(b0)
{
    float gDt;
    float gTime;
    float2 pad1;

    float3 gPlayerPosWS;
    float gPlayerRepelRadius;
};

// 風の設定（単純なグローバル風）
cbuffer WindCB : register(b1)
{
    float gWindTime; // 経過時間
    float gNoiseFreq; // ノイズ空間スケール（WorldPos に掛ける）
    float gPhaseSpread; // ノイズからどれだけ位相をズラすか（ラジアン）
    float gBigWaveWeight; // 1本あたりの高さ（ローカルY の最大値）
    float gWindSpeed; // 風アニメ速度
    float gWindAmplitude; // 揺れの強さ
    float2 gWindDirXZ; // XZ 平面の風向き (正規化済み)
};

// 簡易ハッシュ（Firefly で使っていた物を流用してOK）
uint Hash_u32(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bb;
    x ^= x >> 11;
    x *= 0xac4c1b51;
    x ^= x >> 15;
    x *= 0x31848bab;
    x ^= x >> 14;
    return x;
}

float3 HashDir3(uint s)
{
    float x = (Hash_u32(s * 1664525u + 1013904223u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float y = (Hash_u32(s * 22695477u + 1u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float z = (Hash_u32(s * 1103515245u + 12345u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    return normalize(float3(x, y, z));
}

[numthreads(LEAF_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);
    uint idx = tid.x;
    if (idx >= aliveCount)
        return;

    uint pIndex = gAliveIn[idx];
    LeafParticle p = gParticles[pIndex];

    LeafVolumeGPU vol = gVolumes[p.volumeSlot];

    float dt = gDt;

    // --- 群れ（Volume 中心）への引力 ---
    float3 toCenter = vol.centerWS - p.posWS;
    float distToCenter = length(toCenter);
    float3 dirToCenter = (distToCenter > 1e-4) ? toCenter / distToCenter : float3(0, 1, 0);

    float desiredR = vol.radius;
    float radiusError = distToCenter - desiredR;
    float3 cohesionForce = -dirToCenter * radiusError * 0.3; // 戻す強さ

    // --- 風方向 ---
    float3 windDir = normalize(float3(gWindDirXZ.x, 0.0f, gWindDirXZ.y));
    float3 windForce = windDir * (gWindAmplitude * gWindSpeed * vol.speed);

    // --- ノイズによるひらひら ---
    float3 noiseDir = HashDir3(pIndex * 2654435761u + (uint) (gTime * 60.0));
    noiseDir.y = abs(noiseDir.y); // あまり下に落ち過ぎないよう上向き寄り
    float3 noiseForce = noiseDir * vol.noiseScale;

    // --- プレイヤーの足元を避ける ---
    float3 toPlayer = p.posWS - gPlayerPosWS;
    float distToPlayer = length(toPlayer);
    float3 repel = 0;
    if (distToPlayer < gPlayerRepelRadius && distToPlayer > 0.001)
    {
        float w = 1.0 - (distToPlayer / gPlayerRepelRadius);
        repel = normalize(toPlayer) * (w * 3.0);
    }

    // 合成加速度
    float3 accel = cohesionForce + windForce + noiseForce + repel;

    // 速度更新 & 減衰
    p.velWS += accel * dt;
    p.velWS *= 0.96;

    // 位置更新
    p.posWS += p.velWS * dt;

    // 地面にめり込み過ぎないように高さ調整（Firefly と同じ処理を流用）
    // 例: heightMap から高さをサンプルして少し浮かせるなど
    // （ここは Seigo さんの Firefly 実装に合わせてそのままコピペでOK）

    p.phase += dt;

     // 寿命を減らす
    p.life -= gDt; // 1秒単位の寿命ならこう
    bool outOfLife = (p.life <= 0.0f);

    // 画面外 or Volume外 に出たかどうかなど
    bool outOfBounds = false; // 必要に応じて計算

    bool dead = outOfLife || outOfBounds;

    if (dead)
    {
        // FreeList に id を返却
        gFreeList.Append(pIndex);

        // Volumeごとの数をデクリメント
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    // 生き残ったので書き戻し＆AliveOutにAppend
    gParticles[pIndex] = p;
    gAliveOut.Append(pIndex);
}
