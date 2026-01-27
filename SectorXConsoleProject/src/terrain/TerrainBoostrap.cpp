#include "terrain/TerrainBoostrap.h"
#include "terrain/normalmap_bc5.h"
#include "app/AppConfig.h"
#include "app/texture_registry.h"

#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h> // TerrainClustered / DX11 helpers 一式

namespace TerrainBoot
{
    Result BuildAll(SFW::Graphics::DX11::GraphicsDevice& graphics, int terrainRank)
    {
        using namespace SFW;
        using namespace SFW::Graphics;

        Result r{};

        auto bufferMgr = graphics.GetRenderService()->GetResourceManager<DX11::BufferManager>();
        auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();

        TerrainBuildParams tp;
        tp.cellsX = 256 * terrainRank - 1;
        tp.cellsZ = 256 * terrainRank - 1;
        tp.clusterCellsX = 32;
        tp.clusterCellsZ = 32;
        tp.cellSize = 3.0f;
        tp.heightScale = 80.0f;
        tp.frequency = 1.0f / 90.0f;
        tp.seed = 20251212;
        tp.offset.y -= 40.0f;

        // terrain build
        static std::vector<float> heightMap;
        heightMap.clear();
        static TerrainClustered terrain = TerrainClustered::Build(tp, &heightMap);
        r.terrain = &terrain;
        r.heightMap = heightMap;

        // materials
        static DX11::CommonMaterialResources matRes;
        const uint32_t matIds[4] = { Assets::Mat_Grass, Assets::Mat_Rock, Assets::Mat_Dirt, Assets::Mat_Snow };
        DX11::BuildCommonMaterialSRVs(graphics.GetDevice(), *textureMgr, matIds, &Assets::ResolveTexturePath, matRes);
        r.matRes = &matRes;

        // splat sheet -> per cluster -> array
        uint32_t sheetTexId = Assets::Tex_Splat_Control_0;
        ComPtr<ID3D11Texture2D> sheetTex;
        auto handles = DX11::BuildClusterSplatTexturesFromSingleSheet(
            graphics.GetDevice(), graphics.GetDeviceContext(), *textureMgr,
            sheetTex,
            terrain.clustersX, terrain.clustersZ,
            sheetTexId, &Assets::ResolveTexturePath,
            false
        );

        DX11::AssignClusterSplatsFromHandles(terrain, terrain.clustersX, terrain.clustersZ, handles,
            [](TextureHandle, uint32_t, uint32_t, uint32_t cid) { return (0x70000000u + cid); },
            nullptr
        );

        static DX11::SplatArrayResources splatRes;
        DX11::InitSplatArrayResources(graphics.GetDevice(), splatRes, terrain.clusters.size());
        BuildSplatArrayFromHandles(graphics.GetDevice(), graphics.GetDeviceContext(), *textureMgr, handles, splatRes);
        r.splatRes = &splatRes;

        std::vector<uint32_t> uniqueIds; DX11::CollectUniqueSplatIds(terrain, uniqueIds);
        auto id2slice = DX11::BuildSliceTable(uniqueIds);

        static DX11::ClusterParamsGPU cp{};
        DX11::FillClusterParamsCPU(terrain, id2slice, cp);
        DX11::SetupTerrainGridCB(tp, terrain.clustersX, terrain.clustersZ, sheetTex, cp);
        DX11::BuildOrUpdateClusterParamsSB(graphics.GetDevice(), graphics.GetDeviceContext(), cp);
        DX11::BuildOrUpdateTerrainGridCB(graphics.GetDevice(), graphics.GetDeviceContext(), bufferMgr, cp);
        r.cp = &cp;

        static DX11::CpuImage cpuSplatImage;
        DX11::ReadTexture2DToCPU(graphics.GetDevice(), graphics.GetDeviceContext(), sheetTex.Get(), cpuSplatImage);
        r.cpuSplatImage = &cpuSplatImage;

        // block revert init + build
        static DX11::BlockReservedContext blockRevert;
        bool ok = blockRevert.Init(graphics.GetDevice(),
            L"assets/shader/CS_TerrainClustered.cso",
            L"assets/shader/CS_TerrainClustered_CSMCombined.cso",
            L"assets/shader/CS_WriteArgs.cso",
            L"assets/shader/CS_WriteArgsShadow.cso",
            L"assets/shader/VS_TerrainClusteredGrid.cso",
            L"assets/shader/VS_TerrainClusteredGridDepth.cso",
            L"assets/shader/PS_TerrainClustered.cso",
            (UINT)terrain.indexPool.size());
        assert(ok);
        DX11::BuildFromTerrainClustered(graphics.GetDevice(), terrain, blockRevert);
        r.blockRevert = &blockRevert;

        // HeightTex (R16_UNORM) + SRV
        // NormalTex (BC5) + SRV  ← TerrainUtil::EncodeNormalMapBC5 を使う

        // …（あなたの既存コードをここに移すだけ）

        return r;
    }
}
