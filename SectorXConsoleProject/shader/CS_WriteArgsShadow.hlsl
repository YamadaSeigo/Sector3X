#include "_ShadowTypes.hlsli"

// u0: カスケードの総インデックスバイト数が入っている RAW カウンタ
RWByteAddressBuffer CascadeCounters : register(u0);

// u1: カスケード分の DrawIndirectArgs を詰めて書くバッファ
RWByteAddressBuffer ShadowArgsUAV : register(u1);


[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    // D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS は 5 * 4 bytes
    const uint ARGS_STRIDE_BYTES = 5u * 4u;

    [unroll]
    for (uint c = 0; c < NUM_CASCADES; ++c)
    {
        // カスケード c の総バイト数を取得
        uint totalBytes = CascadeCounters.Load(c * 4u);

        uint indexCount = totalBytes >> 2;

        // このカスケードの Args の書き込み開始位置
        uint base = c * ARGS_STRIDE_BYTES;

        ShadowArgsUAV.Store(base + 0u * 4u, indexCount); // IndexCountPerInstance
        ShadowArgsUAV.Store4(base + 1u * 4u, uint4(1u, 0u, 0u, 0u)); // InstanceCount

    }
}

