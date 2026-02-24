// RainBillboardPS.hlsl

#include "_TileDeferred.hlsli"
#include "_RainParticles.hlsli"

// Light buffers (if you do two-buffers style)
StructuredBuffer<PointLight> gNormalLights : register(t0);
StructuredBuffer<PointLight> gFireflyLights : register(t1);

// Tile lists
StructuredBuffer<uint> gTileLightCount : register(t2);
StructuredBuffer<uint> gTileLightIndices : register(t3);

cbuffer LightingCB : register(b0)
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

cbuffer TiledCB : register(b1)
{
    uint gScreenWidth;
    uint gScreenHeight;
    uint gTilesX;
    uint gTilesY;
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

uint GetTileIndex(float4 posH)
{
    // posH -> NDC
    float2 ndc = posH.xy / posH.w;
    // NDC -> Screen
    float2 screen;
    screen.x = (ndc.x * 0.5f + 0.5f) * gScreenWidth;
    screen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * gScreenHeight; // yは上下反転

    // Screen -> Tile
    uint tileX = min((uint) (screen.x / TILE_SIZE), gTilesX - 1);
    uint tileY = min((uint) (screen.y / TILE_SIZE), gTilesY - 1);
    return tileY * gTilesX + tileX;
}

float3 ShadePointLight(float3 wp, float3 N, float3 albedo, PointLight pl)
{
    float3 toL = pl.positionWS - wp;
    float distSq = dot(toL, toL);
    float range = pl.range;
    float rangeSq = range * range;
    if (range <= 0.0f || distSq >= rangeSq)
        return 0.0f;

    distSq = max(distSq, 1e-6f);
    float invDist = rsqrt(distSq);
    float dist = distSq * invDist;
    float3 L = toL * invDist;

    // cheap smooth falloff (similar to what you used)
    float t = saturate(dist * pl.invRadius);
    float att = 1.0f - t;
    att *= att;

    float ndl = saturate(dot(N, L));

    float3 radiance = pl.color * pl.intensity * att;
    return radiance * (albedo * ndl);
}

float4 main(VSOut input) : SV_Target
{
#ifdef DEBUG_RAIN_HIT_DEPTH
    return float4(input.color, 1.0f); // デバッグ用：衝突している粒子を赤くするなど
#endif

    // uv.y 方向が長さ、uv.x が幅
    // 幅方向：中央が濃く、端が薄い
    float x = abs(input.uv.x * 2.0f - 1.0f);
    float widthMask = saturate(1.0f - x);
    widthMask = widthMask * widthMask;

    // 長さ方向：先端(0)を少し強く、尾(1)を薄く
    float head = 1.0f - input.uv.y;
    float lenMask = smoothstep(0.0f, 0.2f, head) * smoothstep(1.0f, 0.6f, head);

    float alpha = input.a * widthMask * lenMask;
    clip(alpha - 1e-3f); // ほぼ透明なら破棄（閾値は調整）

    uint tileIdx = GetTileIndex(input.posH);
    uint lightCount = min(gTileLightCount[tileIdx], (uint) MAX_LIGHTS_PER_TILE);

    float3 color = gAmbientColor * gAmbientIntensity;

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        uint enc = gTileLightIndices[tileIdx * MAX_LIGHTS_PER_TILE + i];
        uint type = DecodeType(enc);
        uint idx = DecodeIndex(enc);

        PointLight pl;
        if (type == 0)
            pl = gNormalLights[idx];
        else
            pl = gFireflyLights[idx];

        color += ShadePointLight(input.wp, input.N, float3(1.0f, 1.0f, 1.0f), pl); // 雨は白色でシェーディング
    }

    //Premultiplied前提で事前にアルファを乗算しておくと、縁がきれいになる（アルファブレンドの順序に依存しない）
    color *= alpha;

    // 色は白寄り（夜はライトで光らせたいなら別途調整）
    return float4(color, alpha);
}