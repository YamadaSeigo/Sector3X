
#include "_GlobalTypes.hlsli"


//Timeだけ使用
cbuffer SkyCB : register(b6)
{
    float gTime;
    float gRotateSpeed; // rad/sec 例: 0.01
    float2 _pad;
};

cbuffer CameraBuffer : register(b7)
{
    row_major float4x4 invViewProj;
    float4 camForward; //wはpadding
    float4 camPos; // wはpadding
}

cbuffer LightingCB : register(b8)
{
    // Sun / directional
    float3 gSunDirectionWS;
    float gSunIntensity; // 16B
    float3 gSunColor;
    float gAmbientIntensity; // 16B

    // Ambient + counts
    float3 gAmbientColor;
    float emissiveBoost; // Emissiveの強調係数
};


cbuffer FogCB : register(b9)
{
    // Distance fog
    float3 gFogColor; //例: (0.8, 0.8, 1.0)
    float gFogStart; // 例: 100.0 (メートル)
    float gFogEnd; // 例: 3000.0 (メートル)
    float2 _padFog0;
    uint gEnableDistanceFog; // 0/1

    // Height fog
    float gHeightFogBaseHeight; // 霧の基準高さ(この高さ付近が最も濃い想定) 例: 1.0 (地面付近)
    float gHeightFogDensity; // 高さフォグ密度(全体強さ) 例: 0.01
    float gHeightFogFalloff; // 高さ減衰(大きいほど上に行くと急に薄くなる) 例: 0.05
    uint gEnableHeightFog; // 0/1

    // Height fog wind/noise
    float2 gFogWindDirXZ; // 正規化推奨 (x,z)
    float gFogWindSpeed; // 例: 0.2
    float gFogNoiseScale; // 例: 0.08 (ワールド->ノイズ空間)
    float gFogNoiseAmount; // 例: 0.35 (濃淡の強さ 0..1)
    float gFogGroundBand; // 例: 6.0  (地面付近の厚み)
    float gFogNoiseMinHeight; // 例: -1.0 (基準高さから下は強め等)
    float gFogNoiseMaxHeight; // 例: 8.0  (基準高さから上は減衰)
}

// GodRay(光の筋) パラメータ
cbuffer GodRayCB : register(b10)
{
    float2 gSunScreenUV; // 太陽のスクリーンUV(0..1) ※CPUで計算して渡す
    float gGodRayIntensity; // 強さ（例: 0.6）
    float gGodRayDecay; // 減衰（例: 0.96）

    float2 gSunDirSS; // 太陽のスクリーン上の方向ベクトル（正規化済み）
    float2 _padGR0;

    float gGodRayDensity; // 伸び具合（例: 0.9）
    float gGodRayWeight; // サンプル重み（例: 0.02）
    uint gEnableGodRay; // 0/1
    float _padGR1;

    float3 gGodRayTint; // 色（例: (1.0, 0.95, 0.8)）
    float gGodRayMaxDepth; // “空/遠方”判定の深度閾値（例: 0.9995）
};

Texture2D gAlbedoAO : register(t11); // RGB: Albedo, A: Occlusion
Texture2D gNormalRough : register(t12); // RGB: Normal, A: Roughness
Texture2D gEmiMetal : register(t13); // RGB: Emissive, A: Metallic
Texture2D<float> gDepth : register(t14); // Depth

Texture2D<float4> gLightAccum : register(t15);


// 比較サンプラ（ShadowMapService が作っているもの）
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gPointSamp: register(s2);

float ToonStep3(float x)
{
    // 0..1 を 3段階に（好みで値調整）
    // 例: 0, 0.5, 1.0
    return (x < 0.33f) ? 0.0f : (x < 0.66f) ? 0.5f : 1.0f;
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

uint ChooseCascade(float viewDepth)
{
    // viewDepth はカメラ view-space の Z（LHなら +Z が前）、
    // gCascadeSplits[i] には LightShadowService の splitFar[i] を入れる想定

    uint idx = 0;

    // gCascadeCount-1 まで比較し、閾値を超えたら次のカスケードへ
    [unroll]
    for (uint i = 0; i < NUM_CASCADES - 1; ++i)
    {
        if (i < gCascadeCount - 1)
        {
            // viewDepth が split を超えたらインデックスを進める
            idx += (viewDepth > gCascadeSplits[i]) ? 1u : 0u;
        }
    }

    return min(idx, gCascadeCount - 1);
}

float SampleShadowPCF(float4 shadowClip, uint cascade)
{
    float3 ndc = shadowClip.xyz / shadowClip.w;
    float2 uv = ndc.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    float z = ndc.z;

    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return 1.0f;

    const float2 texel = 1.0f / float2(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * texel;
            sum += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + o, cascade), z);
        }
    }
    return sum / 9.0f;
}

float ComputeFogFactor(float viewDepth)
{
    if (gEnableDistanceFog == 0)
        return 0.0f;

    // viewDepth: カメラから前方向の距離(メートル想定)。負になり得るなら abs/viewDepth clamp を入れる
    viewDepth = max(viewDepth, 0.0f);

    float denom = max(gFogEnd - gFogStart, 1e-5f);
    return saturate((viewDepth - gFogStart) / denom);
}

float Hash12(float2 p)
{
    // 低コスト hash（安定）
    float h = dot(p, float2(127.1, 311.7));
    return frac(sin(h) * 43758.5453123);
}

float ValueNoise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float a = Hash12(i);
    float b = Hash12(i + float2(1, 0));
    float c = Hash12(i + float2(0, 1));
    float d = Hash12(i + float2(1, 1));

    // smoothstep
    float2 u = f * f * (3.0f - 2.0f * f);

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}



float FBM2D(float2 p)
{
    // 3~4オクターブで十分（重ければ2でもOK）
    float v = 0.0f;
    float a = 0.5f;
    float2 shift = float2(37.0f, 17.0f);
    [unroll]
    for (int o = 0; o < 4; ++o)
    {
        v += a * ValueNoise2D(p);
        p = p * 2.0f + shift;
        a *= 0.5f;
    }
    return v; // 0..1 付近
}



float GroundBandMask(float yRel)
{
    // yRel = (wp.y - baseHeight)
    // 基準高さから上に行くほど0へ。band内で1に近い。
    float t = saturate(1.0f - (yRel / max(gFogGroundBand, 1e-4f)));
    // 端をなめらかに
    return t * t * (3.0f - 2.0f * t);
}


float ComputeFogNoiseMod(float3 worldPosWS, float timeSec)
{
    float yRel = worldPosWS.y - gHeightFogBaseHeight;

    float h01 = saturate((yRel - gFogNoiseMinHeight) / max(gFogNoiseMaxHeight - gFogNoiseMinHeight, 1e-4f));
    float hMask = (1.0f - h01);

    float band = GroundBandMask(yRel) * hMask;
    if (band <= 0.0001f)
        return 1.0f;

    // ほぼノイズ0なら計算しない
    if (gFogNoiseAmount <= 0.001f || gFogNoiseScale <= 0.0f)
        return 1.0f;

    float2 wind = gFogWindDirXZ * (gFogWindSpeed * timeSec);
    float2 p = (worldPosWS.xz * gFogNoiseScale) + wind;

    float n = FBM2D(p); // 0..1
    float nSigned = (n * 2.0f - 1.0f);

    float mod = 1.0f + (nSigned * gFogNoiseAmount);
    return lerp(1.0f, mod, band);
}



float ComputeHeightFogFactor(float3 camPosWS, float3 worldPosWS, float viewDepth)
{
    if (gEnableHeightFog == 0)
        return 0.0f;

    viewDepth = max(viewDepth, 0.0f);

    float camY = camPosWS.y - gHeightFogBaseHeight;
    float posY = worldPosWS.y - gHeightFogBaseHeight;

    float k = max(gHeightFogFalloff, 1e-5f);
    float e0 = exp(-k * camY);
    float e1 = exp(-k * posY);

    float dy = (worldPosWS.y - camPosWS.y);

    float avgExp;
    if (abs(dy) < 1e-4f)
    {
        float midY = 0.5f * (camY + posY);
        avgExp = exp(-k * midY);
    }
    else
    {
        avgExp = (e0 - e1) / (k * dy);
        avgExp = max(avgExp, 0.0f);
    }

    // ここで密度をノイズ変調（地面付近だけ動く）
    float densityMod = ComputeFogNoiseMod(worldPosWS, gTime);
    float density = gHeightFogDensity * densityMod;

    float opticalDepth = density * viewDepth * avgExp;
    return saturate(1.0f - exp(-opticalDepth));
}


static const int GOD_NUM_SAMPLES = 32;

float ComputeRadialGodRay(float2 uv)
{
    float2 d = abs(gSunScreenUV - 0.5f) * 2.0f;
    float sunIn = saturate(1.0f - max(d.x, d.y));

    // ほとんど見えないなら早期リターン
    [branch]
    if (sunIn <= 0.001f || gGodRayIntensity <= 0.001f || gGodRayWeight <= 0.0f)
        return 0.0f;

    float2 delta = (uv - gSunScreenUV);
    delta *= (gGodRayDensity / (float) GOD_NUM_SAMPLES);

    float illuminationDecay = 1.0f;
    float sum = 0.0f;

    float2 sampUV = uv;

    [loop]
    for (int s = 0; s < GOD_NUM_SAMPLES; ++s)
    {
        sampUV -= delta;

        if (any(sampUV < 0.0f) || any(sampUV > 1.0f))
            break;

        float z = gDepth.SampleLevel(gPointSamp, sampUV, 0);
        float occ = (z >= gGodRayMaxDepth) ? 1.0f : 0.0f;

        sum += occ * illuminationDecay * gGodRayWeight;
        illuminationDecay *= gGodRayDecay;
    }

    return sum * gGodRayIntensity * sunIn;
}

float ComputeDirectionalGodRay(float2 uv, float shadow, float2 sunDirSS)
{
    [branch]
    if (gGodRayIntensity <= 0.001f || gGodRayWeight <= 0.0f)
        return 0.0f;

    const int N = GOD_NUM_SAMPLES;
    float2 stepUV = sunDirSS * (gGodRayDensity / (float) N);

    float sum = 0.0;
    float decay = 1.0;
    float2 p = uv;

    [loop]
    for (int i = 0; i < N; ++i)
    {
        p -= stepUV;

        if (any(p < 0.0) || any(p > 1.0))
            break;

        float z = gDepth.SampleLevel(gPointSamp, p, 0);
        float occ = (z >= gGodRayMaxDepth) ? 1.0 : 0.0;

        float sh = shadow;

        sum += occ * sh * decay * gGodRayWeight;
        decay *= gGodRayDecay;
    }

    return sum * gGodRayIntensity;
}

// 最適化版（入力は正規化されている前提）
float ComputeRadialWeight(float3 camForwardWS, float3 sunDirWS)
{
    float d = abs(dot(camForwardWS, -sunDirWS));
    return smoothstep(0.2, 0.6, d);
}

float4 main(VSOut i) : SV_Target
{
    float2 uv = i.uv;

    float4 albedoAO = gAlbedoAO.Sample(gSampler, uv);
    float4 nr = gNormalRough.Sample(gSampler, uv);
    float4 emiMetal = gEmiMetal.Sample(gSampler, uv);

    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3DのUV(上0) と NDC(Y+上)の差を吸収
    ndc.z = gDepth.Sample(gPointSamp, uv);
    ndc.w = 1.0f;

    // View-Projection 逆行列でワールド位置を復元
    float4 wp = mul(invViewProj, ndc);

    wp /= wp.w;

    float viewDepth = dot(wp.xyz - camPos.xyz, camForward.xyz);

    uint cascade = ChooseCascade(viewDepth);

    float4 shadowClip = mul(gLightViewProj[cascade], wp);
    float vis = SampleShadowPCF(shadowClip, cascade);

    // 影の濃さ(0.7)は元の意図を踏襲
    float shadowMul = lerp(0.6f, 1.0f, vis);

     // GBuffer
    float3 albedo = albedoAO.rgb;
    float3 N = normalize(nr.rgb * 2.0f - 1.0f);

    // -----------------------------
    //  弱いノーマルライティング
    // -----------------------------
    // gSunDirectionWS は「太陽の向き」想定なので、
    // 光が“来る”方向は -gSunDirectionWS として扱う
   // 最適化版（gSunDirectionWS を WS で正規化して渡す前提）
    float3 L = -gSunDirectionWS;

    // 通常の N・L
    float ndl = saturate(dot(N, L));

    // Wrap で少し回り込みを増やして、カートゥーン寄りに
    float wrap = 0.3f; // 0..1 大きいほど回り込みが強い
    float ndlWrap = saturate((ndl + wrap) / (1.0f + wrap));

    // 少しだけカーブさせて中間を強調（好みで調整）
    ndlWrap = pow(ndlWrap, 1.2f);

    // 「どれくらいの強さでノーマル感を出すか」
    // 0 なら完全Unlit、1なら最大（ここでは太陽強度を軽くスケールに利用）
    float detailStrength = saturate(gSunIntensity * 0.5f); // 0..1 くらいを想定

    // 0.85 ~ 1.15 の範囲で明るさを揺らす（弱めの陰影）
    float normalFactorRaw = lerp(0.8f, 1.15f, ndlWrap);

    // detailStrength でフェードイン
    float normalFactor = lerp(1.0f, normalFactorRaw, detailStrength);

    // Ambient（Unlitの基礎明るさ）
    float3 base = albedo * (gAmbientColor * gAmbientIntensity);

    // ノーマルによる“ごつごつ感”をここで掛ける
    base *= normalFactor;

    // 影で暗くする（既存 shadowMul を利用）
    base *= shadowMul;

    // PointLight（Unlit Toon）
    uint2 pix = (uint2) i.pos.xy;
    float3 plAdd = gLightAccum.Load(int3(pix, 0)).rgb;

    // Emissive（必要ならそのまま加算）
    float3 color = base + plAdd + emiMetal.rgb * emissiveBoost;

    // 距離フォグ
    float fogDist = ComputeFogFactor(viewDepth);

    // 高さフォグ
    float fogHeight = ComputeHeightFogFactor(camPos.xyz, wp.xyz, viewDepth);

    // 2つのフォグを合体：1 - (1-a)(1-b) が自然に「どっちも効く」
    float fog = 1.0f - (1.0f - fogHeight) * (1.0f - fogDist);

    color = lerp(color, gFogColor, fog);

    if (gEnableGodRay != 0)
    {
        float wRadial = ComputeRadialWeight(camForward.xyz, gSunDirectionWS);

        float godRadial = ComputeRadialGodRay(uv); // sunScreenUV を使う版
        float godDirectional = ComputeDirectionalGodRay(uv, vis, gSunDirSS); // sunDirSS を使う版

        float god = lerp(godDirectional, godRadial, wRadial);

        // fogが無いとほぼ見えない／霧があると見える（好みで係数調整）
        float fogVis = saturate(fog * 1.2f);

        // 太陽の色味で足す（Unlitスタイルなら加算でOK）
        color += gGodRayTint * god * fogVis;
    }

    return float4(color, 1.0f);
}
