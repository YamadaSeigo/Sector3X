#include "_FireflyParticles.hlsli"

AppendStructuredBuffer<uint> gFreeList : register(u0);

cbuffer CBSpawn : register(b0)
{
    float3 gPlayerPosWS;
    float gTime;

    uint gActiveVolumeCount;
    uint gMaxSpawnPerVolumePerFrame; // —áF32
    uint gMaxParticles;
    uint pad0;
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
