#include "_FireflyParticles.hlsli"

StructuredBuffer<FireflyVolumeGPU> gVolumes : register(t0);

// AliveIn は SRV で読む
StructuredBuffer<uint> gAliveIn : register(t1);

// aliveCount は raw buffer
ByteAddressBuffer gAliveCountRaw : register(t2);

Texture2D<float> gHeightMap : register(t3);

SamplerState gHeightSamp : register(s0);

// particles
RWStructuredBuffer<FireflyParticle> gParticles : register(u0);

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
    float2 pad;

    float3 gPlayerPosWS;
    float gPlayerRepelRadius;
};

cbuffer TerrainGridCB : register(b1)
{
    float2 gOriginXZ; // ワールド座標の基準 (x,z)
    float2 gCellSizeXZ; // 1クラスタのサイズ (x,z)
    uint gDimX; // クラスタ数X
    uint gDimZ; // クラスタ数Z
    float heightScale;
    uint _pad11;
};

cbuffer CBParams : register(b2)
{
    float gDamping; // 速度減衰係数
    float gWanderFreq; // ふわふわノイズ周波数
    float gWanderStrength; // ふわふわノイズ強さ
    float gCenterPull; // ボリューム中心への引き戻し強さ
    float gGroundBand; // 地面からの高さ帯
    float gGroundPull; // 地面付近への引き戻し強さ
    float gMaxSpeed; // 速度上限
    float pad3;

    float gBurstStrength; // 例：3.0
    float gBurstRadius; // 例：4.0（プレイヤーから何mまで強いか）
    float gBurstSwirl; // 例：1.5（渦成分）
    float gBurstUp; // 例：1.0（上方向の押し上げ）
};

float SampleGroundY(float2 xz)
{
    float2 terrainSize = gCellSizeXZ * float2(gDimX, gDimZ);
    float2 uv = saturate((xz - gOriginXZ) / terrainSize);
    return gHeightMap.SampleLevel(gHeightSamp, uv, 0);
}

// 軽いハッシュ乱数（0..1）
uint Hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

float3 HashDir3(uint s)
{
    float x = (Hash_u32(s * 1664525u + 1013904223u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float y = (Hash_u32(s * 22695477u + 1u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    float z = (Hash_u32(s * 1103515245u + 12345u) & 0x00FFFFFFu) / 16777216.0f * 2 - 1;
    return normalize(float3(x, y, z));
}

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);
    uint i = tid.x;
    if (i >= aliveCount)
        return;

    uint id = gAliveIn[i];

    FireflyParticle p = gParticles[id];

    // Volume外に出たら戻す（or kill）
    FireflyVolumeGPU v = gVolumes[p.volumeSlot];

    // -------------------------
    // 1) forces (加速度/インパルス)
    // -------------------------

    // (A) プレイヤー反応：通常 repel（弱め、dtスケール）
    {
        float3 toP = p.posWS - gPlayerPosWS;
        float d = length(toP);
        if (d < gPlayerRepelRadius && d > 1e-3f)
        {
            float x = (gPlayerRepelRadius - d) / gPlayerRepelRadius; // 0..1
            float k = x * x; // 近いほど強く（滑らか）
            float3 dir = toP / d;
            p.velWS += dir * (k * 1.2f) * gDt; // 係数は調整（今の0.5より少し強め）
        }
    }

    // (B) 侵入瞬間バースト：短時間だけ “衝撃波＋渦＋上昇”
    if (v.burstT > 0.0f)
    {
        float3 toP = p.posWS - gPlayerPosWS;
        float d = length(toP);

        // プレイヤー中心から burstRadius 以内だけ強い
        if (d < gBurstRadius && d > 1e-3f)
        {
            float x = 1.0f - (d / gBurstRadius); // 1..0
            float w = x * x; // 近いほど強い
            float t = v.burstT; // 1..0（時間で減衰）

            float3 dir = toP / d;

            // “ぶわっ” の主成分：外向きインパルス（dtを掛けずに一発感も作れるが、安定のため少しdtで）
            // 迫力が足りなければ gDt を外して「瞬間加算」に寄せてもOK
            p.velWS += dir * (gBurstStrength * w * t);

            // 渦（円周方向）で「吹き飛び粒子」ではなく「群れが開く」感じにする
            float3 axis = HashDir3(v.seed ^ 0xBADC0FFEu); // volume固定の傾いた軸
            float3 tang = normalize(cross(axis, dir));
            p.velWS += tang * (gBurstSwirl * w * t);

            // 少し上に持ち上げる（草から舞い上がる）
            p.velWS.y += (gBurstUp * w * t);
        }
    }

    // (C) ふわふわノイズ
    {
        float tt = gTime + p.phase;
        float3 wander =
        float3(sin(tt * gWanderFreq + 1.0f),
               sin(tt * gWanderFreq * 0.9f + 2.0f),
               sin(tt * gWanderFreq * 1.1f + 3.0f)) * gWanderStrength;
        p.velWS += wander * gDt;
    }

    // (D) 中心へ弱い引き戻し（散らばり過ぎ抑制）
    {
        float3 toC = (v.centerWS - p.posWS);
        p.velWS += toC * (gCenterPull * gDt);
    }

    // (E) 地面帯へ寄せる（草の上を漂う）
    {
        float groundY = SampleGroundY(p.posWS.xz);
        float band = gGroundBand + (p.band01 - 0.5f) * 1.0f; // ±0.5m の分散（調整）
        float targetY = groundY + band;
        float dy = targetY - p.posWS.y;
        p.velWS.y += dy * (gGroundPull * gDt);
    }

    // -------------------------
    // 2) damping / clamp
    // -------------------------

    p.velWS *= exp(-gDamping * gDt);

    float sp = length(p.velWS);
    if (sp > gMaxSpeed)
        p.velWS *= (gMaxSpeed / sp);

    // -------------------------
    // 3) integrate
    // -------------------------
    p.posWS += p.velWS * gDt;

    // 寿命
    p.life -= gDt * 0.05f;

    float3 rel = p.posWS - v.centerWS;
    float rr = v.radius * v.radius;
    bool outDot = dot(rel, rel) > rr;

    bool dead = (p.life <= 0.0f) || outDot;

    if (dead)
    {
        // FreeListへ返す
        gFreeList.Append(id);
        // slot count 減らす
        InterlockedAdd(gVolumeCount[p.volumeSlot], (uint) -1);
        return;
    }

    gParticles[id] = p;
    gAliveOut.Append(id);
}
