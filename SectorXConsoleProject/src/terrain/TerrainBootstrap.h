#pragma once
#include <vector>
#include <wrl/client.h>

#include <SectorFW/Graphics/RenderTypes.h>

using Microsoft::WRL::ComPtr;

namespace SFW::Graphics {
    struct TerrainBuildParams;
    struct TerrainClustered;
}
namespace SFW::Graphics::DX11 {
    class TextureManager;
    class BufferManager;
    struct CommonMaterialResources;
    struct SplatArrayResources;
    struct ClusterParamsGPU;
    struct CpuImage;
    struct BlockReservedContext;
}
namespace SFW::Graphics::DX11 { struct HeightTexMapping; }
namespace SFW::Graphics { struct HeightTexMapping; }
namespace SFW::Graphics::DX11 { class GraphicsDevice; }

namespace TerrainBoot
{
    struct Result
    {
		SFW::Graphics::TerrainBuildParams params{};
        SFW::Graphics::TerrainClustered* terrain = nullptr; // Build が static を返すならポインタで扱う
        std::vector<float> heightMap;

        // GPU/CPU resources
        SFW::Graphics::DX11::CommonMaterialResources* matRes = nullptr;
        SFW::Graphics::DX11::SplatArrayResources* splatRes = nullptr;
        SFW::Graphics::DX11::ClusterParamsGPU* cp = nullptr;
        SFW::Graphics::DX11::CpuImage* cpuSplatImage = nullptr;
        SFW::Graphics::DX11::BlockReservedContext* blockRevert = nullptr;

        // textures
        SFW::Graphics::TextureHandle heightTexHandle{};
		SFW::Graphics::TextureHandle normalTexHandle{};
        ComPtr<ID3D11ShaderResourceView> heightMapSRV;
        ComPtr<ID3D11ShaderResourceView> normalMapSRV;
    };

    /**
	 * @brief 地形のビルドと関連リソースの生成を行う関数
	 * @param graphics DX11のグラフィックスデバイス。リソース生成に必要。
	 * @param terrainRank 地形のランク。ビルドの詳細やリソースの品質に影響を与える可能性があります。
	 * @return Result ビルドされた地形と関連リソースを含む構造体。ビルドに失敗した場合は、nullptr や空の値が含まれる可能性があります。
     */
    Result BuildAll(
        SFW::Graphics::DX11::GraphicsDevice& graphics,
        int terrainRank
    );
}
