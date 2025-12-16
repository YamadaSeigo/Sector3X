#include "_GlobalTypes.hlsli"

cbuffer SkyCB : register(b12)
{
    float gTime;
    float gRotateSpeed; // rad/sec 例: 0.01
    float2 _pad;
};

static const float PI = 3.14159265;

float2 DirToEquirectUV(float3 d)
{
    // d: normalized
    float u = atan2(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5;
    return float2(u, v);
}

float3 RotateY(float3 v, float a)
{
    float s = sin(a), c = cos(a);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// 0..1 の乱数（入力が同じなら常に同じ値）
float Hash12(float2 p)
{
    // 軽量ハッシュ（定番）
    float h = dot(p, float2(127.1, 311.7));
    return frac(sin(h) * 43758.5453123);
}

// dir は normalize 済み想定
float StarTwinkle(float2 uv, float time,
                  int2 grid, // 例: 120.0 (大きいほど細かくなる)
                  float minHz, float maxHz, // 瞬き速度範囲 例: 0.2..1.2
                  float amp)          // 例: 0.35
{
   // セルIDを作る（uvは0..1なので本来負にならない）
    uint2 cell;
    cell.x = (uint) floor(uv.x * (float) grid.x);
    cell.y = (uint) floor(uv.y * (float) grid.y);

    // 念のため範囲内に丸める（uv=1.0 ちょうど対策）
    cell.x = min(cell.x, grid.x - 1u);
    cell.y = min(cell.y, grid.y - 1u);

    // 星ごとの乱数
    float r0 = Hash12(cell);
    float r1 = Hash12(cell + 17.0);

    // 星ごとの周波数と位相
    float hz = lerp(minHz, maxHz, r0);
    float phase = 6.2831853 * r1;

    float w = 0.5 + 0.5 * sin(time * hz * 6.2831853 + phase);

    // 1 ± amp に変換（明るさ係数）
    return 1.0 + (w - 0.5) * 2.0 * amp;
}


struct VSOutput
{
    float4 clip : SV_POSITION;
    float3 dir : TEXCOORD;
};

float4 main(VSOutput i) : SV_Target
{
    float3 dir = normalize(i.dir);

    float a = gTime * gRotateSpeed;
    float3 d = RotateY(dir, a);

    float2 uv = DirToEquirectUV(d);

    float4 baseColor = baseColorFactor;
    if ((hasFlags & FLAG_HAS_BASECOLORTEX) != 0u)
        baseColor *= gBaseColorTex.Sample(gSampler, uv);

    float4 emissionColor = float4(0, 0, 0, 0);
    if ((hasFlags & FLAG_HAS_EMISSIVETEX) != 0u)
        emissionColor = gEmissiveTex.Sample(gSampler, uv);

    float4 col = baseColor + emissionColor;

     // 「星だけ」明滅させたいのでマスクを作る（白いほど星、みたいな）
    float starMask = saturate(col.rgb.r); // 例：単純にRを使う（テクスチャに合わせて調整）

    // 瞬き係数（dirベース + time）
    float tw = StarTwinkle(uv, gTime,
                           int2(512, 512), // cellScale
                           0.2, 1.4, // minHz/maxHz
                           0.6); // amp

    // 星部分だけ強弱
    col.rgb *= lerp(1.0, tw, starMask);

    return col;
}


