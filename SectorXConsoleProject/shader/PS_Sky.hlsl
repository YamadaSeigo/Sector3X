#include "_GlobalTypes.hlsli"

cbuffer SkyCB : register(b9)
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

float Hash13(float3 p)
{
    // 簡易 3D hash
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float StarTwinkle_Dir(float3 d, float time, float density,
                      float minHz, float maxHz, float amp)
{
    // density: 例 400～1200（大きいほど細かい）
    float3 q = floor(d * density);
    float r0 = Hash13(q);
    float r1 = Hash13(q + 17.0);

    float hz = lerp(minHz, maxHz, r0);
    float phase = 6.2831853 * r1;
    float w = 0.5 + 0.5 * sin(time * hz * 6.2831853 + phase);

    return 1.0 + (w - 0.5) * 2.0 * amp;
}


struct VSOutput
{
    float4 clip : SV_POSITION;
    float3 dir : TEXCOORD;
};

PS_PRBOutput main(VSOutput i)
{
    PS_PRBOutput output;

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

    output.AlbedoAO = baseColor;

     // 「星だけ」明滅させたいのでマスクを作る（白いほど星、みたいな）
    float starMask = saturate(emissionColor.r); // 例：単純にRを使う（テクスチャに合わせて調整）

    // 瞬き係数（dirベース + time）
    float tw = StarTwinkle_Dir(d, gTime, 1200.0, 0.8, 2.0, 1.2);

    // 星部分だけ強弱
    emissionColor.rgb *= lerp(1.0, tw, starMask);

    output.EmissionMetallic = emissionColor;

    output.NormalRoughness = float4(dir, 0.0f);

    return output;
}


