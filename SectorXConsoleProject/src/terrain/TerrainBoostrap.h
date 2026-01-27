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
        SFW::Graphics::TerrainClustered* terrain = nullptr; // Build ‚ª static ‚ð•Ô‚·‚È‚çƒ|ƒCƒ“ƒ^‚Åˆµ‚¤
        std::vector<float> heightMap;

        // GPU/CPU resources
        SFW::Graphics::DX11::CommonMaterialResources* matRes = nullptr;
        SFW::Graphics::DX11::SplatArrayResources* splatRes = nullptr;
        SFW::Graphics::DX11::ClusterParamsGPU* cp = nullptr;
        SFW::Graphics::DX11::CpuImage* cpuSplatImage = nullptr;
        SFW::Graphics::DX11::BlockReservedContext* blockRevert = nullptr;

        // textures
        SFW::Graphics::TextureHandle heightTexHandle{};
        ComPtr<ID3D11ShaderResourceView> heightMapSRV;
        ComPtr<ID3D11ShaderResourceView> normalMapSRV;
    };

    Result BuildAll(
        SFW::Graphics::DX11::GraphicsDevice& graphics,
        int terrainRank
    );
}
