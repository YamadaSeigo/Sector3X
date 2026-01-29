#include "_GlobalTypes.hlsli"

struct PSInput
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    // 中心からの距離（UV空間）
    //float2 d = input.uv - float2(0.5, 0.5);
    //float r = length(d);

    //float ar = 1.0f - input.color.a;
    //float a = saturate(r - ar);

    //float4 texColor = gBaseColorTex.Sample(gSampler, input.uv);
    //float4 finalColor = input.color * texColor;

    //finalColor.a *= a;

    //return finalColor;

    float4 tex = gBaseColorTex.Sample(gSampler, input.uv);

    // 進行度: col.a(1→0) を 0→1 に変換
    float p = saturate(1.0 - input.color.a);

    // UV中心からの距離
    float2 d = input.uv - float2(0.5, 0.5);
    float dist = length(d);

    // UV中心→四隅の最大距離は約0.7071
    const float maxR = 0.70710678;

    // pに応じて不透明半径を増やす（maxRadius01=1なら最終的に全体）
    float R = maxR * p;

    // 内側=0 / 外側=1（境界をsoftnessでぼかす）
    float mask = smoothstep(R, R + 1.0f * p/*softness*/, dist);

    float4 outCol = tex * input.color; // 色は頂点カラーも反映
    outCol.a = mask; // “中心から広がる”フェードイン

    return outCol;
}
