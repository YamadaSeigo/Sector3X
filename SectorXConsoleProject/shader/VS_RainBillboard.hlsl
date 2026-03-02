// RainBillboard.hlsl

#include "_RainParticles.hlsli"

StructuredBuffer<RainParticle> gRain : register(t0);
StructuredBuffer<uint> gAlive : register(t1);


cbuffer RainRenderCB : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamPosWS;
    float _pad0;

    float gBaseWidth; // 例: 0.01～0.03 (m)
    float gBaseLength; // 例: 0.10～0.40 (m)
    float gSpeedToLength; // 例: 0.01～0.03 (length += |v| * k)
    float gMinSpeedForDir; // 例: 0.1

    float gAlpha; // 全体alpha
    float gLifeFade; // 例: 1.0 (寿命で薄くする強さ)
    float2 _pad3;
};

struct VSOut
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
    float a : TEXCOORD1;
    float3 wp : TEXCOORD2; // world position
    float3 N : TEXCOORD3; // normal
#ifdef DEBUG_RAIN_HIT_DEPTH
    float3 color : TEXCOORD4; // デバッグ用（衝突している粒子を赤くするなど）
#endif
};


float3 SafeNormalize(float3 v, float3 fallback)
{
    float l2 = dot(v, v);
    if (l2 < 1e-8f)
        return fallback;
    return v * rsqrt(l2);
}

static const float2 kCornerCCW[4] =
{
    float2(-0.5f, 0.0f),
    float2(0.5f, 0.0f),
    float2(-0.5f, 1.0f),
    float2(0.5f, 1.0f),
};

// tri list: (0,1,2) (2,1,3)
static const uint kVidToCorner[6] = { 0, 1, 2, 2, 1, 3 };

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;

    uint pid = gAlive[iid];
    RainParticle p = gRain[pid];

    // 速度方向（雨筋の軸）
    float speed = length(p.velWS);
    float3 velDir = (speed > gMinSpeedForDir) ? (p.velWS / speed) : float3(0, -1, 0);

    // カメラへ向かうベクトル
    float3 toCam = SafeNormalize(gCamPosWS - p.posWS, float3(0, 0, 1));

    // velocity方向に伸びた“カメラ向きのリボン”にする：
    // right = toCam × velDir（カメラ方向と速度方向の両方に直交）
    float3 right = cross(toCam, velDir);
    float r2 = dot(right, right);
    if (r2 < 1e-8f)
    {
        // 速度がカメラ方向とほぼ平行なときのフォールバック
        // 適当な軸で right を作る
        float3 a = (abs(velDir.y) < 0.99f) ? float3(0, 1, 0) : float3(1, 0, 0);
        right = cross(a, velDir);
    }
    right = SafeNormalize(right, float3(1, 0, 0));

    // サイズ：幅は base + addSize、長さは base + |v|*k
    float width = max(0.0f, gBaseWidth + p.addSize);
    float length = max(0.0f, gBaseLength + speed * gSpeedToLength);

    // 4頂点（クアッド）生成：vid=0..3
    // uv.x: 幅方向（-0.5..+0.5）
    // uv.y: 長さ方向（0..1） ※ 0が上（新しい）/1が下（古い）っぽくすると自然
    uint c = kVidToCorner[vid];
    float2 corner = kCornerCCW[c];

    // 速度方向に伸ばす：上端を粒子位置にして、下へ伸ばす（velDirの反対側）
    // 雨は velDir が下向きなので、ここは「+velDir * (-t)」でも良いが、
    // “尾”を velDir 方向に伸ばす方が自然なので t=corner.y をそのまま使う
    float3 ws =
        p.posWS
        + right * (corner.x * width)
        + velDir * (corner.y * length);

    o.posH = mul(gViewProj, float4(ws, 1.0f));
    o.uv = float2(corner.x + 0.5f, corner.y);

    // 寿命でフェード（lifeを0..secondsで持っている前提）
    // ここでは「寿命が短いほど薄い」例：lifeが0に近いと消える
    float lifeFade = saturate(p.life * gLifeFade);
    o.a = gAlpha * lifeFade;
    o.wp = ws;
    o.N = toCam; // 法線はカメラ方向を向いていると仮定（シェーダーで適宜調整）

#ifdef DEBUG_RAIN_HIT_DEPTH
    o.color = lerp(float3(1,1,1), float3(1,0,0), p.debugHit / 255.0f); // デバッグ用（衝突している粒子を赤くするなど）
#endif

    return o;
}