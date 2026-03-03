cbuffer WetnessScrollCB : register(b0)
{
    int2 gScrollTexel; // New(x,y) = Prev(x + dx, y + dy)
    float gInitWetness; // 新規領域の初期値（0 or GlobalWetness）
    
    float pad1;
    
    uint2 gTexSize; // (W,H)

    uint2 pad2;
};

Texture2D<float> gPrev : register(t0);
RWTexture2D<float> gNew : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x, y = tid.y;
    if (x >= gTexSize.x || y >= gTexSize.y)
        return;

    int2 src = int2(x, y) + gScrollTexel;
    float v = gInitWetness;

    if (src.x >= 0 && src.y >= 0 && src.x < (int) gTexSize.x && src.y < (int) gTexSize.y)
        v = gPrev.Load(int3(src, 0));

    gNew[uint2(x, y)] = v;
}