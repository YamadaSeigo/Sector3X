#include "_LeafParticles.hlsli"

StructuredBuffer<LeafParticle> gParticles : register(t0);
StructuredBuffer<uint> gAlive : register(t1);
StructuredBuffer<LeafVolumeGPU> gVolumes : register(t2);

cbuffer CBCamera : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamRightWS;
    float gBaseSize; // billboard half-size 例: 0.05
    float3 gCamUpWS;
    float gTime;

    float3 gCameraPosWS;
    float _padCam0;
    float2 gNearFar; // (near, far)  ※線形化に使う
    uint gDepthIsLinear01; // 1: すでに線形(0..1) / 0: D3Dのハードウェア深度
    float _padCam1;
};

struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
    float3 col : COLOR0;
};

static const float2 kCornerCCW[4] =
{
    float2(-1, -1),
    float2(1, -1),
    float2(-1, 1),
    float2(1, 1),
};

// tri list: (0,1,2) (2,1,3)
static const uint kVidToCorner[6] = { 0, 1, 2, 2, 1, 3 };

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;

    uint pid = gAlive[iid];
    LeafParticle p = gParticles[pid];
    LeafVolumeGPU v = gVolumes[p.volumeSlot];

    uint c = kVidToCorner[vid];
    // corner
    float2 s = kCornerCCW[c];

    float2 v2 = float2(dot(p.velWS, gCamRightWS), dot(p.velWS, gCamUpWS));
    float vlen2 = dot(v2, v2);
    float velAngle = (vlen2 > 1e-6) ? atan2(v2.y, v2.x) : 0.0f;

    // ランダム回転（phase）と速度方向をブレンド
    float angle = lerp(p.phase, velAngle, 0.7f); // 0.0=ランダム優先, 1.0=速度優先

    float sn, cs;
    sincos(angle, sn, cs);

    // ビルボード平面内で回転
    float2 sr;
    sr.x = s.x * cs - s.y * sn;
    sr.y = s.x * sn + s.y * cs;

    float life01 = saturate(p.life / max(p.life0, 1e-6));

    // 末尾だけフェード（例：最後の25%）
    float fadeStart = 0.25; // 0.25=最後25%
    float a = saturate(life01 / fadeStart); // 0..1
    a = a * a * (3 - 2 * a); // smoothstep

    float size = gBaseSize + p.size;
    size *= a; // フェードに応じてサイズも縮小

    float3 worldPos =
    p.posWS +
    gCamRightWS * (sr.x * size) +
    gCamUpWS * (sr.y * size);

    o.posH = mul(gViewProj, float4(worldPos, 1));

    // uvは単純マップ
    o.uv = (s * 0.5f + 0.5f);

    float3 baseCol = v.color * v.intensity;

    // 粒子固有色を乗算（p.tint を追加した前提）
    baseCol *= p.tint;

    o.col = baseCol;

#ifdef DEBUG_HIT_DEPTH
    float hit = p.debugHit / 255.0;
    o.col = lerp(o.col, float3(1, 0, 0), hit); // 赤く
#endif

    return o;
}