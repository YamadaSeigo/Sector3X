cbuffer BloomCB : register(b0)
{
    float gBloomThreshold; // 例: 1.0（HDR前提） / LDRなら0.8等
    float gBloomKnee; // 例: 0.5（soft threshold）
    float gBloomIntensity; // 合成側で使ってもOK
    float gBloomMaxDist; // 最大ブルーム距離 
};

cbuffer CameraBuffer : register(b1)
{
    row_major float4x4 invViewProj;
    float4 camForward; //wはpadding
    float4 camPos; // wはpadding
}

Texture2D gSceneColor : register(t0);
Texture2D<float> gDepth : register(t1); // Depth
SamplerState gLinearClamp : register(s0);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// “soft threshold”
// https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/ みたいな形の定番
float SoftThreshold(float luma, float threshold, float knee)
{
    float t = luma - threshold;
    float soft = saturate(t / max(knee, 1e-5f));
    // knee内はなめらかに立ち上げ
    return soft * soft * (3.0f - 2.0f * soft);
}

// 距離減衰（0..gBloomMaxDist までは 1→0 に落ちる）
float ComputeBloomDistanceAtten(float viewDepth)
{
    viewDepth = max(viewDepth, 0.0f);

    if (gBloomMaxDist <= 1e-4f)
        return 1.0f; // パラメータがおかしいとき保険

    float t = saturate(viewDepth / gBloomMaxDist);
    // ちょっと滑らかに落としたいなら smoothstep 風に
    t = t * t * (3.0f - 2.0f * t); // 0→1のS字
    return 1.0f - t; // 近く1, 遠く0
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = gSceneColor.Sample(gLinearClamp, uv).rgb;

    // Bright抽出（従来通り）
    float l = Luminance(c);
    float w = SoftThreshold(l, gBloomThreshold, gBloomKnee);

    // depth → NDC → ワールド → viewDepth 復元
    float depth = gDepth.Sample(gLinearClamp, uv);

    float4 ndc;
    ndc.xy = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D UVとNDCのY反転
    ndc.z = depth;
    ndc.w = 1.0f;

    float4 wp = mul(invViewProj, ndc);
    wp /= wp.w;

    float3 toP = wp.xyz - camPos.xyz;
    float viewDepth = dot(toP, camForward.xyz); // カメラ前方への距離（メートル）

    // Bloom距離減衰
    float distAtten = ComputeBloomDistanceAtten(viewDepth);

    float3 bloomSrc = c * w * distAtten;

    return float4(bloomSrc, 1);
}
