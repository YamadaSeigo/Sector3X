// ClusterPipeline.hpp
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
using Microsoft::WRL::ComPtr;

namespace SFW::Graphics
{

    // ==== 共有構造体（HLSL と一致するよう 16byte アライン推奨） ====
#pragma pack(push, 1)
    struct ClusterInfoCPU {
        uint32_t indexStart;
        uint32_t indexCount;
        uint32_t bucketId;
        uint32_t flags;
        float aabbMin[3];
        float geomError;
        float aabbMax[3];
        uint32_t pad0;
    };
#pragma pack(pop)

    struct Bucket {
        // AppendStructuredBuffer<uint> 相当（UAV with counter）
        ComPtr<ID3D11Buffer>            visibleBuffer;
        ComPtr<ID3D11UnorderedAccessView> visibleUAV;
        ComPtr<ID3D11ShaderResourceView>  visibleSRV;
        // Indirect args
        ComPtr<ID3D11Buffer>            indirectArgs; // D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS
        uint32_t                        indexCountPerInstance = 0;
        uint32_t                        startIndexLocation = 0; // IB 上の開始
        int32_t                         baseVertexLocation = 0;
        uint32_t                        startInstanceLocation = 0; // 通常 0
    };

    struct ClusterPipeline
    {
        static constexpr uint32_t kMaxBuckets = 32;
        static constexpr uint32_t kMaxClusters = 1u << 20;

        // 共有リソース
        ComPtr<ID3D11Buffer>            clusterInfoBuf;
        ComPtr<ID3D11ShaderResourceView> clusterInfoSRV;

        // バケツ
        std::vector<Bucket> buckets;

        // シェーダ
        ComPtr<ID3D11ComputeShader> csCull;
        ComPtr<ID3D11VertexShader>  vsTerrain;
        ComPtr<ID3D11PixelShader>   psTerrain;
        ComPtr<ID3D11InputLayout>   layout;

        // 定数バッファ（ViewCB, LodCB, OcclCB）
        ComPtr<ID3D11Buffer> cbView, cbLod, cbOccl;

        // IB/VB
        ComPtr<ID3D11Buffer> vb;
        ComPtr<ID3D11Buffer> ib;
        DXGI_FORMAT           ibFormat = DXGI_FORMAT_R32_UINT;

        // ---------- 作成 ----------
        void Create(ID3D11Device* dev,
            const std::vector<ClusterInfoCPU>& clusters,
            uint32_t vertexStride,
            const void* vbData, size_t vbBytes,
            const uint32_t* ibData, size_t ibBytes,
            const std::vector<std::tuple<uint32_t/*bucketId*/, uint32_t/*indexCount*/, uint32_t/*startIndex*/>>& bucketDefs);

        // 毎フレーム
        void RunCullingAndDraw(ID3D11DeviceContext* ctx,
            const float viewProj[16],
            float viewportW, float viewportH,
            float projScale,
            float tauIn, float tauOut,
            bool useMOC);
    };

}