// Raw‚ÈCountBuffer‚É CopyStructureCount ‚ª‘‚¢‚½ uint ‚ğ“Ç‚Ş
ByteAddressBuffer gAliveCountRaw : register(t0);

// DrawInstancedIndirect ‚Ì args ‚ğ raw UAV ‚É‘‚­
RWByteAddressBuffer gDrawArgsRaw : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint aliveCount = gAliveCountRaw.Load(0);

    // D3D11_DRAW_INSTANCED_INDIRECT_ARGS:
    // uint VertexCountPerInstance;
    // uint InstanceCount;
    // uint StartVertexLocation;
    // uint StartInstanceLocation;

    gDrawArgsRaw.Store(0, 6); // 6 verts per instance (two triangles)
    gDrawArgsRaw.Store(4, aliveCount); // instanceCount
    gDrawArgsRaw.Store(8, 0);
    gDrawArgsRaw.Store(12, 0);
}
