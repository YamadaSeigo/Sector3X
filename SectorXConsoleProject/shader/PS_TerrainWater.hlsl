#include "_GlobalTypes.hlsli"

struct VSOut
{
    float4 pos : SV_Position;
    float3 posWS : TEXCOORD0;
    float3 posVS : TEXCOORD1;
    float viewDepth : TEXCOORD2;
    float2 uv : TEXCOORD3; // 0..1 (normal texture 用)
};

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

cbuffer FrameCB : register(b9)
{
    row_major float4x4 gInvProj;

    float3 gCameraPosWS;
    float gTime;


    float4 gWaterTint; // rgb=color, a=unused
    float4 gShallowColor; // optional
    float4 gDeepColor; // optional
    float4 gFogColor; // optional

    float2 gNormalTiling0;
    float2 gNormalTiling1;

    float2 gFlowDir0;
    float2 gFlowDir1;

    float gFlowSpeed0;
    float gFlowSpeed1;
    float gNormalStrength;
    float gSpecPower;

    float gDepthColorScale; // 深さ→色変化の強さ
    float gWaterAlpha; // 水面アルファ
    float gFogStart; // 霧開始距離
    float gFogEnd; // 霧終了距離

    float2 gScreenSize; // (width, height)
    float gShoreFadeScale; // 岸の明るさ調整
    float gPad1;

    float4 gFoamColor; // 白に少し黄緑や青を混ぜても良い
    float gFoamDepthStart; // 例: 0.02
    float gFoamDepthEnd; // 例: 0.25
    float gFoamIntensity; // 例: 0.8
    float gFoamNoiseScale; // 例: 6.0
};


Texture2D gNormalMap0 : register(t20);
Texture2D gNormalMap1 : register(t21);
Texture2D gSceneDepthTex : register(t22);

SamplerState gWrapSampler : register(s3);
SamplerState gPointClamp : register(s4);

float3 DecodeNormal(Texture2D tex, float2 uv)
{
    float2 xy = tex.Sample(gWrapSampler, uv).rg * 2.0f - 1.0f;
    float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy, z);
}

// depth tex の値 [0,1] を view-space z に戻す
float ViewSpaceZFromDeviceDepth(float deviceDepth)
{
    float2 ndc;
    ndc.x = 0.0f;
    ndc.y = 0.0f;

    float4 p;
    p.xy = ndc.xy;
    p.z = deviceDepth * 2.0f - 1.0f;
    p.w = 1.0f;

    float4 viewPos = mul(p, gInvProj);
    return viewPos.z / viewPos.w;
}


float4 main(VSOut pin) : SV_TARGET
{

    float2 uv0 = pin.uv * gNormalTiling0 + gFlowDir0 * (gTime * gFlowSpeed0);
    float2 uv1 = pin.uv * gNormalTiling1 + gFlowDir1 * (gTime * gFlowSpeed1);

    float3 n0TS = DecodeNormal(gNormalMap0, uv0);
    float3 n1TS = DecodeNormal(gNormalMap1, uv1);

    float3 nTS;
    nTS.xy = (n0TS.xy + n1TS.xy) * 0.5f * gNormalStrength;
    nTS.z = sqrt(saturate(1.0f - dot(nTS.xy, nTS.xy)));

    float3 T = float3(1, 0, 0);
    float3 B = float3(0, 0, 1);
    float3 Up = float3(0, 1, 0);
    float3x3 tbn = float3x3(T, B, Up);

    float3 N = normalize(mul(nTS, tbn));
    float3 V = normalize(gCameraPosWS - pin.posWS);
    float3 L = normalize(-gSunDirectionWS);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float diffuse = 0.35f + 0.65f * NdotL;

    float spec = pow(saturate(dot(N, H)), gSpecPower);
    spec *= 0.15f; // 反射なしでも少しキラッと見せる

    // -----------------------------
    // 画面UV
    // -----------------------------
    float2 screenUV = pin.posWS.xy / gScreenSize;
    screenUV.y = 1.0f - screenUV.y;

    // 法線で少し深度参照位置を歪ませると水っぽい
    float2 depthDistort = N.xz * 0.01f;
    float2 depthUV = saturate(screenUV + depthDistort);

    // -----------------------------
    // 背景深度を取得
    // -----------------------------
    float sceneDeviceDepth = gSceneDepthTex.Sample(gPointClamp, depthUV).r;

    // 水面自身の view-space z
    float waterViewZ = pin.posVS.z;

    // 背景の view-space z
    float sceneViewZ = ViewSpaceZFromDeviceDepth(sceneDeviceDepth);

    // DirectX view space は通常前方が +z か -z か実装次第なので、
    // 差分は abs で扱う
    float waterToBottom = abs(sceneViewZ - waterViewZ);

    // 水深らしい量
    float depth01 = saturate(waterToBottom * gDepthColorScale);

    // -----------------------------
    // 浅瀬 / 深場の色
    // -----------------------------
    float3 baseColor = lerp(gShallowColor.rgb, gDeepColor.rgb, depth01);
    baseColor *= gWaterTint.rgb;

    // 岸辺は少し明るく
    float shore = 1.0f - saturate(waterToBottom * gShoreFadeScale);
    baseColor += shore * 0.08f;

    // -----------------------------
    // 水面ライティング
    // -----------------------------
    float3 litColor = baseColor * diffuse + spec;

    // -----------------------------
    // 浅瀬フォーム
    // -----------------------------
    float foamMask = 1.0f - smoothstep(gFoamDepthStart, gFoamDepthEnd, waterToBottom);

    // 法線マップをノイズ代わりに使う簡易版
    float foamNoise = gNormalMap0.Sample(
        gWrapSampler,
        pin.uv * gFoamNoiseScale + gFlowDir0 * (gTime * 0.03f)
    ).r;

    // ムラをつける
    foamMask *= smoothstep(0.35f, 0.8f, foamNoise);

    // 少しだけ角度で抑えると見栄えが安定しやすい
    float foamViewFade = saturate(1.0f - dot(N, V));
    foamMask *= lerp(0.7f, 1.0f, foamViewFade);

    // 色を加算 or lerp
    litColor = lerp(litColor, gFoamColor.rgb, foamMask * gFoamIntensity);

    // -----------------------------
    // 距離フォグ
    // -----------------------------
    float viewDist = length(pin.posVS);
    float fogFactor = saturate((viewDist - gFogStart) / max(0.0001f, (gFogEnd - gFogStart)));
    float3 finalColor = lerp(litColor, gFogColor.rgb, fogFactor);

    finalColor *= (gAmbientColor * gAmbientIntensity);

    return float4(finalColor, gWaterAlpha);
}