cbuffer WetnessUpdateCB : register(b0)
{
    float gDt;
    float gDryRate; // 例: 0.05
    float gRainRate; // 例: 0.3
    float gGlobalWet; // 境界対策に使うなら
    uint2 gTexSize;
    float2 pad2;
};

// いったん遮蔽なし（後で RainOcclusionHeightMap を足す）
RWTexture2D<float> gWet : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x, y = tid.y;
    if (x >= gTexSize.x || y >= gTexSize.y)
        return;

    float wet = gWet[uint2(x, y)];

    // 乾燥（指数減衰の近似：exp(-k*dt)）
    wet *= exp(-gDryRate * gDt);

    // 降雨（遮蔽なし版）
    wet += gRainRate * gDt;

    wet = saturate(wet);
    gWet[uint2(x, y)] = wet;
}