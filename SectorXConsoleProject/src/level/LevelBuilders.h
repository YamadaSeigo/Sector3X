#pragma once
#include <memory>

#include "graphics/RenderDefine.h"
#include <SectorFW/Core/Level.hpp>

namespace SFW::ECS { class ServiceLocator; }
namespace SFW::Graphics { struct BufferHandle; struct TextureHandle; struct TerrainBuildParams; }
namespace App { struct Context; }

namespace Levels
{
    void EnqueueGlobalSystems(WorldType& world);

    void EnqueueTitleLevel(WorldType& world, App::Context& ctx);
    void EnqueueLoadingLevel(WorldType& world, App::Context& ctx, const char* loadingName);

    struct OpenFieldLevelParams
    {
        SFW::Graphics::BufferHandle gridHandle;
        SFW::Graphics::TextureHandle heightTexHandle;
        Graphics::TerrainBuildParams& terrainParams;
		SFW::Graphics::TerrainClustered& terrainClustered;
        Graphics::DX11::CpuImage& cpuSplatImage;
        std::vector<float>& heightMap;
		int terrainRank = 0;
	};

    void EnqueueOpenFieldLevel(WorldType& world, App::Context& ctx, const OpenFieldLevelParams& params);
}
