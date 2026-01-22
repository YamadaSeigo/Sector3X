
AppendStructuredBuffer<uint> gFreeList : register(u0);

cbuffer CBInit : register(b0)
{
    uint gMaxParticles;
    uint3 pad0;
};

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    if (i >= gMaxParticles)
        return;

    // ‹ó‚«ID‚Æ‚µ‚ÄÏ‚Ş
    gFreeList.Append(i);
}
