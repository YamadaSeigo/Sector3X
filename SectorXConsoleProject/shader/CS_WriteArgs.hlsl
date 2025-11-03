RWByteAddressBuffer Counter : register(u0);

RWByteAddressBuffer ArgsUAV : register(u1);

[numthreads(1, 1, 1)]
void main(uint3 dtid: SV_DispatchThreadID)
{
    uint bytes = Counter.Load(0);
    uint vtxCount = bytes >> 2; // 4 bytes per uint index
    ArgsUAV.Store4(0, uint4(vtxCount, 1, 0, 0));
}
