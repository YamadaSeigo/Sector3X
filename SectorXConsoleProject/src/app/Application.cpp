
#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>
#include <SectorFW/Graphics/PointLightService.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>
#include <SectorFW/Graphics/TerrainOccluderExtraction.h>

#include "Application.h"
#include "component_registration.h"
#include "PlayerService.h"

#include "../level/LevelBuilders.h"

#include "../graphics/RenderDefine.h"
#include "../graphics/RenderPipeline.h"
#include "../graphics/DeferredRenderingService.h"
#include "../graphics/SpriteAnimationService.h"
#include "../environment/EnvironmentService.h"
#include "../environment/RainService.h"
#include "../environment/LeafService.h"
#include "../environment/FireflyService.h"
#include "../environment/WindService.h"

namespace App
{
	Context Application::m_ctx{};
	std::unique_ptr<SFW::SimpleThreadPool> Application::m_threadPool{};
	GraphicsDeviceType Application::m_graphics{};
	TerrainBoot::Result Application::m_terrainRes{};
    TerrainWater Application::m_terrainWater{};
    std::unique_ptr<Graphics::I3DPerCameraService> Application::m_perCameraService{};
    std::unique_ptr<Graphics::LightShadowService> Application::m_lightShadowService{};
    std::unique_ptr<WindService> Application::m_windService{};
    std::unique_ptr<DeferredRenderingService> Application::m_deferredRenderingService{};
    std::unique_ptr<Graphics::DX11::LightShadowResourceService> Application::m_lightShadowResourceService{};
	std::unique_ptr<EnvironmentService> Application::m_environmentService{};
	std::unique_ptr<FireflyService> Application::m_fireflyService{};
	std::unique_ptr<LeafService> Application::m_leafService{};
    std::unique_ptr<RainService> Application::m_rainService{};
	std::unique_ptr<SFW::TimerService> Application::m_timerService{};
	std::unique_ptr<GameEngineType> Application::m_gameEngine{};
	ComPtr<ID3D11SamplerState> Application::m_linearSampler{};
	ComPtr<ID3D11SamplerState> Application::m_pointSampler{};
	ComPtr<ID3D11ShaderResourceView> Application::m_leafTextureSRV{};
	Graphics::HeightTexMapping Application::m_heightTexMap{};


    bool Application::Initialize()
    {

		// 念のため、複数回呼ばれても初期化処理が走らないようにする
		static bool initialized = false;
		if (initialized) return false;
		initialized = true;

        App::RegisterComponents();

        if (!InitializeWindow()) return false;
        if (!InitializeGraphics()) return false;
        if (!InitializeTerrain()) return false;
        if (!InitializeWater()) return false;
        if (!InitializePhysics()) return false;
        if (!InitializeServices()) return false;

        RegisterRenderCallbacks();

		if (!InitializeParticleResources()) return false;
		if (!InitializeContext()) return false;
        if (!InitializeRenderPipeline()) return false;
        if (!InitializeGameEngine()) return false;

        RegisterDebugUI();
        LoadInitialLevel();

        m_threadPool = std::make_unique<SFW::SimpleThreadPool>();

        return true;
    }

    bool Application::InitializeWindow()
    {
		// DPI Awareness を設定する。これを設定しないと、
        // WindowsのDPIスケーリングが100%以外の環境でウィンドウサイズやマウスポインタの位置がおかしくなる
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        
        WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);
        return true;
    }

    bool Application::InitializeGraphics()
    {
        m_graphics.Configure<SFW::Debug::ImGuiBackendDX11Win32>(
            WindowHandler::GetMainHandle(),
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            FPS_LIMIT
        );

        return true;
    }

    bool Application::InitializeTerrain()
    {
        m_terrainRes = TerrainBoot::BuildAll(m_graphics, m_terrainRank);

        m_heightTexMap =
            Graphics::MakeHeightTexMappingFromTerrainParams(
                m_terrainRes.params,
                m_terrainRes.heightMap
            );

        return true;
    }

    bool Application::InitializeWater()
    {
        TerrainWater::BuilderParams param{};

        param.heigthMapPath = "assets/texture/terrain/WaterHeight.png";
        param.normal1Path = "assets/texture/terrain/WaterNormalHigh.jpeg";
        param.normal2Path = "assets/texture/terrain/OceanNormal.jpg";

        param.worldMapSizeX =
            (m_terrainRes.params.cellsX + 1) * m_terrainRes.params.cellSize;

        param.worldMapSizeZ =
            (m_terrainRes.params.cellsZ + 1) * m_terrainRes.params.cellSize;

        param.worldOffset = m_terrainRes.params.offset;
        param.heightScale = m_terrainRes.params.heightScale;
        param.clusterCellsX = 16;
        param.clusterCellsZ = 16;
        param.cellSize = 6.0f;

        m_terrainWater.BuildCluster(param);

        m_terrainWater.CompileShader(
            m_graphics.GetDevice(),
            L"assets/shader/CS_TerrainWater.cso",
            L"assets/shader/CS_WriteArgs.cso",
            L"assets/shader/VS_TerrainWater.cso",
            L"assets/shader/PS_TerrainWater.cso"
        );

        m_terrainWater.CreateResource(m_graphics);

        return true;
    }

    bool Application::InitializePhysics()
    {
        Physics::PhysicsDevice::InitParams params{};
        params.maxBodies = 100000;
        params.maxBodyPairs = 64 * 1024;
        params.maxContactConstraints = 2 * 1024;
        params.workerThreads = -1;

        if (!m_physics.Initialize(params))
        {
            SFW_ASSERT(false && "Failed Physics Device Initialize");
            return false;
        }

        Physics::PhysicsService::Plan physicsPlan =
        {
            1.0f / static_cast<float>(FPS_LIMIT),
            1,
            false
        };

        m_physicsService = std::make_unique<Physics::PhysicsService>(
            m_physics,
            m_shapeManager,
            physicsPlan,
			1u << 14 // コマンドキューの容量 (2の累乗)
        );

        return true;
    }

    bool Application::InitializeServices()
    {
        auto renderService = m_graphics.GetRenderService();

        auto bufferMgr =
            renderService->GetResourceManager<Graphics::DX11::BufferManager>();

        auto textureMgr =
            renderService->GetResourceManager<Graphics::DX11::TextureManager>();

        m_winInput = std::make_unique<Input::WinInput>(
            WindowHandler::GetMouseInput()
        );

        m_perCameraService =
            std::make_unique<Graphics::DX11::PerCamera3DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        m_perCameraService->SetFarClip(1500.0f);

        m_ortCameraService =
            std::make_unique<Graphics::DX11::OrtCamera3DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        m_camera2DService =
            std::make_unique<Graphics::DX11::Camera2DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        m_lightShadowService =
            std::make_unique<Graphics::LightShadowService>();

        Graphics::LightShadowService::CascadeConfig cascadeConfig{};
        cascadeConfig.shadowMapResolution =
        {
            static_cast<float>(SHADOW_MAP_SIZE),
            static_cast<float>(SHADOW_MAP_SIZE)
        };
        cascadeConfig.cascadeCount = 3;
        cascadeConfig.shadowDistance = 80.0f;
        cascadeConfig.casterExtrusion = 0.0f;

        m_lightShadowService->SetCascadeConfig(cascadeConfig);

        m_windService = std::make_unique<WindService>(bufferMgr);

        m_playerService = std::make_unique<PlayerService>(bufferMgr);

        {
            const auto& tp = m_terrainRes.params;

            Math::Vec3f playerLocation =
            {
                tp.cellsX * tp.cellSize * 0.26f,
                0.0f,
                tp.cellsZ * tp.cellSize * 0.19f
            };

            m_terrainRes.terrain->SampleHeightNormalBilinear(
                playerLocation.x,
                playerLocation.z,
                playerLocation.y
            );

            playerLocation.y += 2.0f;
            m_playerService->SetPlayerPosition(playerLocation);
        }

        m_audioService = std::make_unique<Audio::AudioService>();

        if (!m_audioService->Initialize())
        {
            assert(false && "Failed Audio Service Initialize");
            return false;
        }

        auto device = m_graphics.GetDevice();
        auto deviceContext = m_graphics.GetDeviceContext();

        m_deferredRenderingService =
            std::make_unique<DeferredRenderingService>(
                device,
                bufferMgr,
                textureMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT,
                L"assets/shader/CS_TileFrustumGen.cso",
                L"assets/shader/CS_TileCulling_TwoBuffers.cso",
                L"assets/shader/CS_TileLightingAccum.cso"
            );

        m_lightShadowResourceService =
            std::make_unique<Graphics::DX11::LightShadowResourceService>();

        Graphics::DX11::ShadowMapConfig shadowMapConfig{};
        shadowMapConfig.width = SHADOW_MAP_SIZE;
        shadowMapConfig.height = SHADOW_MAP_SIZE;

        if (!m_lightShadowResourceService->Initialize(device, shadowMapConfig))
        {
            assert(false && "Failed ShadowMapService Initialize");
            return false;
        }

        m_pointLightService =
            std::make_unique<Graphics::PointLightService>();

        m_environmentService =
            std::make_unique<EnvironmentService>(bufferMgr);

        m_spriteAnimationService =
            std::make_unique<SpriteAnimationService>(bufferMgr);

        m_fireflyService =
            std::make_unique<FireflyService>(
                device,
                deviceContext,
                bufferMgr,
                L"assets/shader/CS_ParticleInitFreeList.cso",
                L"assets/shader/CS_FireflySpawn.cso",
                L"assets/shader/CS_FireflyUpdate.cso",
                L"assets/shader/CS_ParticleArgs.cso",
                L"assets/shader/VS_FireflyBillboard.cso",
                L"assets/shader/PS_Firefly.cso"
            );

        m_leafService =
            std::make_unique<LeafService>(
                device,
                deviceContext,
                bufferMgr,
                L"assets/shader/CS_ParticleInitFreeList.cso",
                L"assets/shader/CS_LeafClumpUpdate.cso",
                L"assets/shader/CS_LeafSpawn.cso",
                L"assets/shader/CS_LeafUpdate.cso",
                L"assets/shader/CS_ParticleArgs.cso",
                L"assets/shader/VS_LeafBillboard.cso",
                L"assets/shader/PS_Leaf.cso"
            );

        m_rainService =
            std::make_unique<RainService>(
                device,
                deviceContext,
                bufferMgr,
                L"assets/shader/CS_ParticleInitFreeList.cso",
                L"assets/shader/CS_RainSpawn.cso",
                L"assets/shader/CS_RainUpdate.cso",
                L"assets/shader/CS_ParticleArgs.cso",
                L"assets/shader/VS_RainBillboard.cso",
                L"assets/shader/PS_Rain.cso",
                L"assets/shader/CS_WetnessScrollCopy.cso",
                L"assets/shader/CS_WetnessUpdate.cso"
            );

        m_timerService = std::make_unique<SFW::TimerService>();

        return true;
    }

    bool Application::InitializeParticleResources()
    {
        auto textureMgr =m_graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>();

        Graphics::DX11::TextureCreateDesc texDesc{};
        texDesc.path = "assets/texture/sprite/Leaf.png";
        texDesc.forceSRGB = true;

        Graphics::TextureHandle texHandle;
        textureMgr->Add(texDesc, texHandle);

        auto texData = textureMgr->Get(texHandle);
        m_leafTextureSRV = texData.ref().srv;

        return true;
    }

    bool App::Application::InitializeContext()
    {
        m_ctx.graphics = &m_graphics;
        m_ctx.renderService = m_graphics.GetRenderService();
        m_ctx.shadowRes = m_lightShadowResourceService.get();
        m_ctx.pointLight = m_pointLightService.get();
        m_ctx.deferred = m_deferredRenderingService.get();
        m_ctx.wind = m_windService.get();
        m_ctx.player = m_playerService.get();
        m_ctx.env = m_environmentService.get();
        m_ctx.firefly = m_fireflyService.get();
        m_ctx.leaf = m_leafService.get();
        m_ctx.rain = m_rainService.get();

        return true;
	}

    bool App::Application::InitializeRenderPipeline()
    {
        m_graphics.ExecuteCustomFunc(
            [this](
                auto* rg,
                auto& mainRTV,
                auto& mainDSV,
                auto& mainDSVRO,
                auto& mainDepthSRV
                )
            {
                m_ctx.mainDepthSRV = mainDepthSRV;

                RenderPipe::Initialize(
                    rg,
                    m_ctx,
                    mainRTV,
                    mainDSV,
                    mainDSVRO,
                    mainDepthSRV,

                    [](uint64_t frame)
                    {
                        DrawTerrainColor(frame);
                    },

                    [](uint64_t frame)
                    {
                        DrawOpaqueParticle(frame);
                    },

                    [](uint64_t frame)
                    {
                        DrawTransparentParticle(frame);
                    }
                );
            }
        );

        return true;
    }

    bool App::Application::InitializeGameEngine()
    {
        ECS::ServiceLocator locator(
			m_graphics.GetRenderService(),
			m_physicsService.get(),
            m_winInput.get(),
            m_perCameraService.get(),
            m_ortCameraService.get(),
            m_camera2DService.get(),
            m_lightShadowService.get(),
            m_windService.get(),
            m_playerService.get(),
            m_audioService.get(),
            m_deferredRenderingService.get(),
            m_lightShadowResourceService.get(),
            m_environmentService.get(),
			m_pointLightService.get(),
            m_spriteAnimationService.get(),
            m_fireflyService.get(),
            m_leafService.get(),
            m_rainService.get(),
            m_timerService.get()
        );

        locator.InitAndRegisterStaticService<SpatialChunkRegistry>();

		WorldType world(std::move(locator));

		m_gameEngine = std::make_unique<GameEngineType>(std::move(m_graphics), std::move(world), App::FPS_LIMIT);

        return true;
    }

    void App::Application::Run()
    {
        WindowHandler::Run([]()
            {
                m_gameEngine->MainLoop(m_threadPool.get());
            });
    }

    int App::Application::Shutdown()
    {
        m_threadPool.reset();
        return WindowHandler::Destroy();
    }


    void App::Application::RegisterRenderCallbacks()
    {
        auto renderService = m_graphics.GetRenderService();

        renderService->SetCustomUpdateFunction(
            [](Graphics::RenderService* renderService)
            {
                bool execute = m_ctx.executeCustom.load(std::memory_order_relaxed);
                if (!execute) return;

                auto viewProj = m_perCameraService->GetCameraBufferDataNoLock().viewProj;
                auto camPos = m_perCameraService->GetEyePos();

                auto resolution = m_perCameraService->GetResolution();
                uint32_t width = (uint32_t)resolution.x;
                uint32_t height = (uint32_t)resolution.y;

                static Graphics::DefaultLodSelector lodSel = {};

                // ---- 高さメッシュ（粗）オプション ----
                Graphics::HeightCoarseOptions2 hopt{};
                hopt.upDotMin = 0.65f;
                hopt.maxSlopeTan = 5.0f; // 垂直近い面は除外
                hopt.heightClampMin = -4000.f;
                hopt.heightClampMax = +8000.f;
                // 自動LOD（セル解像度）
                hopt.gridLod.minCells = 2;  //クラスター内の最小セル数
                hopt.gridLod.maxCells = 8; //クラスター内の最大セル数
                hopt.gridLod.targetCellPx = 128.f;
                // 高さバイアス
                hopt.bias.baseDown = 0.05f;  // 常に5cm下げる
                hopt.bias.slopeK = 0.00f;  // 斜面で追加ダウン

                // ---- 画面占有率・LOD 等 ----
                Graphics::OccluderExtractOptions opt{};
                opt.viewProj = viewProj.data();
                opt.viewportW = width;
                opt.viewportH = height;
                opt.cameraPos = camPos;
                opt.minAreaPx = 2000.f;
                opt.maxClusters = 64;
                opt.backfaceCull = true;
                opt.maxDistance = 200.0f;

                std::vector<uint32_t> clusterIds;
                std::vector<Graphics::SoftTriWorld> trisW;
                std::vector<Graphics::SoftTriClip>  trisC;

                // ---- ハイブリッド抽出 ----
                ExtractOccluderTriangles_HeightmapCoarse_Hybrid(
                    *m_terrainRes.terrain, m_heightTexMap, hopt, opt, clusterIds, trisW, &trisC);

                // MOCバインディング
                auto MyMOCRender = [renderService](const float* packedXYZW, uint32_t vertexCount,
                    const uint32_t* indices, uint32_t indexCount,
                    uint32_t vpW, uint32_t vpH)
                    {
                        Graphics::MocTriBatch tris =
                        {
                            packedXYZW,			//const float* clipVertices = nullptr; // (x, y, z, w) 配列
                            indices,			//const uint32_t* indices = nullptr;   // インデックス配列
                            vertexCount / 3,	//uint32_t      numTriangles = 0;
                            true				//bool          valid = true;          // 近クリップ全面裏などなら false
                        };

                        renderService->RenderingOccluderInMOC(tris);
                    };

                // MOCにオクル―ダーを描画
                Graphics::DispatchToMOC(MyMOCRender, trisC, width, height);
            }
        );

        renderService->SetCustomPreDrawFunction(
            [](Graphics::RenderService* renderService, uint32_t slot)
            {
                bool execute = m_ctx.executeCustom.load(std::memory_order_relaxed);
                if (!execute) return;

                auto deviceContext = m_graphics.GetDeviceContext();

                //auto viewProj = perCameraService->GetCameraBufferData().viewProj;
                auto camPos = m_perCameraService->GetEyePos();
                Math::Frustumf frustumPlanes
                    //Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
                    = m_perCameraService->MakeFrustum(true);

                auto resolution = m_perCameraService->GetResolution();
                uint32_t width = (uint32_t)resolution.x;
                uint32_t height = (uint32_t)resolution.y;

                m_graphics.SetDepthStencilState(Graphics::DepthStencilStateID::Default);
                m_graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

                deviceContext->VSSetSamplers(3, 1, m_linearSampler.GetAddressOf());

                static Graphics::DX11::BlockReservedContext::ShadowDepthParams shadowParams{};

                shadowParams.mainDSV = m_graphics.GetMainDepthStencilView().Get();
                shadowParams.mainViewProj = m_perCameraService->MakeViewProjMatrix();
                memcpy(shadowParams.mainFrustumPlanes, frustumPlanes.data(), sizeof(shadowParams.mainFrustumPlanes));
                auto& cascadeDSV = m_lightShadowResourceService->GetCascadeDSV();
                for (int c = 0; c < Graphics::kMaxShadowCascades; ++c) {
                    shadowParams.cascadeDSV[c] = cascadeDSV[c].Get();
                }

                auto& cascade = m_lightShadowService->GetCascades();
                memcpy(shadowParams.lightViewProj, cascade.lightViewProj.data(), sizeof(shadowParams.lightViewProj));
                shadowParams.cascadeFrustumPlanes = cascade.frustumWS;

                // フラスタムをライトの逆方向に押し出して影の判定を緩める
                auto pushDir = m_lightShadowService->GetDirectionalLight().directionWS * -1.0f;
                float lenDot = pushDir.normalized().dot({ 0.0f, 1.0f,0.0f });

                // 垂直になるほど大きくなる
                float pushLen = 200.0f * (1.0f - std::abs(lenDot));

                for (auto& fru : shadowParams.cascadeFrustumPlanes) {
                    fru = fru.PushedAlongDirection(pushDir, pushLen);
                }

                shadowParams.screenW = App::WINDOW_WIDTH;
                shadowParams.screenH = App::WINDOW_HEIGHT;

                // シャドウマップ用のSRVを解除
                constexpr ID3D11ShaderResourceView* nullSRV = nullptr;
                deviceContext->PSSetShaderResources(7, 1, &nullSRV);

                m_lightShadowResourceService->ClearDepthBuffer(deviceContext);

                //雨用の深度マップもクリアしておく
                m_rainService->ClearDepthMap(deviceContext);

                //CBの5, Samplerの1にバインド
                m_lightShadowResourceService->BindShadowResources(deviceContext, 5);

                auto bufMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
                auto cameraHandle = bufMgr->FindByName(Graphics::DX11::PerCamera3DService::BUFFER_NAME);
                ComPtr<ID3D11Buffer> cameraCB;
                {
                    auto cameraBufData = bufMgr->Get(cameraHandle);
                    cameraCB = cameraBufData->buffer;
                }

                m_terrainRes.blockRevert->RunShadowDepth(deviceContext,
                    cameraCB,
                    m_terrainRes.heightMapSRV,
                    m_terrainRes.normalMapSRV,
                    shadowParams,
                    *m_terrainRes.cp,
                    &m_lightShadowResourceService->GetCascadeViewport(), false);

                // 地形の水面の可視インデックスを計算してバッファに書き込む
                TerrainWater::ComputeContext computeCtx{};
                computeCtx.devCtx = deviceContext;
                computeCtx.mainFrustum = frustumPlanes;
                computeCtx.viewProj = shadowParams.mainViewProj;
                computeCtx.screenSize[0] = App::WINDOW_WIDTH;
                computeCtx.screenSize[1] = App::WINDOW_HEIGHT;

                m_terrainWater.ComputeVisibleIndices(computeCtx, cameraCB);

                // 雨用に上からの地形の遮蔽マップを描画しておく
                Graphics::DX11::TerrainPatchCB patchCBData{};
                patchCBData.gPatchCenterXZ = { camPos.x, camPos.z };
                patchCBData.gPatchHalfSize = m_rainService->GetSpawnRadius();
                patchCBData.gPatchVertsX = 32;
                patchCBData.gPatchVertsZ = 32;

                m_terrainRes.blockRevert->RunPatchDepth(deviceContext,
                    *m_terrainRes.cp,
                    patchCBData,
                    m_rainService->GetMatrixCB(),
                    m_terrainRes.heightMapSRV,
                    m_rainService->GetDepthMapDSV()
                );

                // 雨の湿り気マップを更新
                m_rainService->UpdateWetnessCS(deviceContext);
            }
        );
    }

    void App::Application::RegisterDebugUI()
    {
        //シーンロードのデバッグコールバック登録

        static std::string newLevelName;

        BIND_DEBUG_TEXT("Level", "Name", &newLevelName);

        static bool loadAsync = false;

        BIND_DEBUG_CHECKBOX("Level", "loadAsync", &loadAsync);

        REGISTER_DEBUG_BUTTON("Level", "load", [](bool) {
            auto& worldRequestService = m_gameEngine->GetWorld().GetRequestServiceNoLock();

            if (loadAsync) {
                //ローディング中のレベルを先にロード
                auto loadingCmd = worldRequestService.CreateLoadLevelCommand(App::LOADING_LEVEL_NAME, false);
                worldRequestService.PushCommand(std::move(loadingCmd));
            }

            //ロード完了後のコールバック
            auto loadedFunc = [](WorldType::Session* pSession) {

                //ローディングレベルをクリーンアップ
                pSession->CleanLevel(App::LOADING_LEVEL_NAME);
                };

            auto reqCmd = worldRequestService.CreateLoadLevelCommand(newLevelName, loadAsync, true, loadAsync ? loadedFunc : nullptr);
            worldRequestService.PushCommand(std::move(reqCmd));
            });

        REGISTER_DEBUG_BUTTON("Level", "clean", [](bool) {
            auto& worldRequestService = m_gameEngine->GetWorld().GetRequestServiceNoLock();
            auto reqCmd = worldRequestService.CreateCleanLevelCommand(newLevelName);
            worldRequestService.PushCommand(std::move(reqCmd));
            });
    }

    void App::Application::LoadInitialLevel()
    {
		auto& world = m_gameEngine->GetWorld();

        Levels::EnqueueGlobalSystems(world);
        Levels::EnqueueLoadingLevel(world, m_ctx, App::LOADING_LEVEL_NAME);
        Levels::EnqueueTitleLevel(world, m_ctx);
        static Levels::OpenFieldLevelParams openFieldParams =
        {
            .gridHandle = m_terrainRes.cp->gridHandle,
            .heightTexHandle = m_terrainRes.heightTexHandle,
            .terrainParams = m_terrainRes.params,
            .terrainClustered = *m_terrainRes.terrain,
            .cpuSplatImage = *m_terrainRes.cpuSplatImage,
            .heightMap = m_terrainRes.heightMap,
            .terrainRank = m_terrainRank
        };

        Levels::EnqueueOpenFieldLevel(world, m_ctx, openFieldParams);
        //初めのレベルをロード
        {
            world.LoadLevel("Title");
        }
    }

    void App::Application::DrawTerrainColor(uint64_t frame)
    {
        if (!m_ctx.executeCustom.load(std::memory_order_relaxed))
            return;

        m_graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        m_graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);
        auto deviceContext = m_graphics.GetDeviceContext();

        deviceContext->VSSetSamplers(3, 1, m_linearSampler.GetAddressOf());
        deviceContext->PSSetSamplers(3, 1, m_pointSampler.GetAddressOf());

        Graphics::DX11::BindCommonMaterials(
            deviceContext,
            *m_terrainRes.matRes
        );

        ID3D11ShaderResourceView* splatSrv =
            m_terrainRes.splatRes->splatArraySRV.Get();

        deviceContext->PSSetShaderResources(28, 1, &splatSrv);

        ID3D11ShaderResourceView* biomeSrv =
            m_terrainRes.splatRes->biomeArraySRV.Get();

        deviceContext->PSSetShaderResources(30, 1, &biomeSrv);

        deviceContext->RSSetViewports(1, &m_graphics.GetMainViewport());

        m_terrainRes.blockRevert->RunColor(
            deviceContext,
            m_terrainRes.heightMapSRV,
            m_terrainRes.normalMapSRV,
            *m_terrainRes.cp
        );
    }

    void App::Application::DrawOpaqueParticle(uint64_t frame)
    {
        auto* deviceContext = m_graphics.GetDeviceContext();

        m_graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        m_graphics.SetBlendState(Graphics::BlendStateID::Additive);

        ID3D11RenderTargetView* rtvs[DeferredTextureCount] = {};

        {
            ID3D11DepthStencilView* currentDsv = nullptr;

            deviceContext->OMGetRenderTargets(
                DeferredTextureCount,
                rtvs,
                &currentDsv
            );

            if (currentDsv)
                currentDsv->Release();

            ID3D11DepthStencilView* readOnlyDsv =
                m_graphics.GetMainDepthStencilViewReadOnly().Get();

            deviceContext->OMSetRenderTargets(
                DeferredTextureCount,
                rtvs,
                readOnlyDsv
            );
        }

        const uint32_t slot =
            static_cast<uint32_t>(frame % Graphics::RENDER_BUFFER_COUNT);

        // ホタルのスポーンと描画
        m_fireflyService->SpawnDrawParticles(
            deviceContext,
            m_terrainRes.heightMapSRV,
            m_terrainRes.cp->cbGrid,
            slot
        );

        ComPtr<ID3D11Buffer> windCb;

        {
            auto windHandle = m_windService->GetBufferHandle();

            auto windBufferData =
                m_graphics
                .GetRenderService()
                ->GetResourceManager<Graphics::DX11::BufferManager>()
                ->Get(windHandle);

            windCb = windBufferData->buffer;
        }

        ComPtr<ID3D11ShaderResourceView> mainDepthSRV =
            m_graphics.GetMainDepthStencilSRV();

        // 葉っぱのスポーン
        m_leafService->SpawnDrawParticles(
            deviceContext,
            m_terrainRes.heightMapSRV,
            mainDepthSRV,
            m_terrainRes.cp->cbGrid,
            windCb,
            slot
        );

        // 葉っぱは不透明扱い
        m_graphics.SetBlendState(Graphics::BlendStateID::Opaque);
        m_graphics.SetDepthStencilState(Graphics::DepthStencilStateID::Default);

        deviceContext->OMSetRenderTargets(
            DeferredTextureCount,
            rtvs,
            m_graphics.GetMainDepthStencilView().Get()
        );

        m_leafService->DrawParticles(
            deviceContext,
            m_leafTextureSRV
        );

        for (auto* rtv : rtvs)
        {
            if (rtv)
                rtv->Release();
        }
    }

    void App::Application::DrawTransparentParticle(uint64_t frame)
    {
        auto* deviceContext = m_graphics.GetDeviceContext();

        m_graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        m_graphics.SetBlendState(Graphics::BlendStateID::Premultiplied);

        {
            ID3D11RenderTargetView* rtvs[DeferredTextureCount] = {};
            ID3D11DepthStencilView* currentDsv = nullptr;

            deviceContext->OMGetRenderTargets(
                DeferredTextureCount,
                rtvs,
                &currentDsv
            );

            if (currentDsv)
                currentDsv->Release();

            ID3D11DepthStencilView* readOnlyDsv =
                m_graphics.GetMainDepthStencilViewReadOnly().Get();

            deviceContext->OMSetRenderTargets(
                DeferredTextureCount,
                rtvs,
                readOnlyDsv
            );

            for (auto* rtv : rtvs)
            {
                if (rtv)
                    rtv->Release();
            }
        }

        RainParticlePool::TiledLightData tiledLightData{};

        tiledLightData.normalLightSRV =
            m_lightShadowResourceService->GetPointLightSRV().Get();

        tiledLightData.fireflyLightSRV =
            m_fireflyService->GetPointLightSRV();

        auto tileLightList =
            m_deferredRenderingService->GetTileLightList(false);

        deviceContext->VSSetSamplers(
            3,
            1,
            m_linearSampler.GetAddressOf()
        );

        auto fogCB = m_environmentService->GetFogCBData();

        m_terrainWater.Render(
            deviceContext,
            m_lightShadowResourceService->GetLightDataCB(),
            m_graphics.GetMainDepthStencilSRV(),
            Math::Inverse(m_perCameraService->GetCameraBufferDataNoLock().proj),
            m_perCameraService->GetEyePos(),
            m_timerService->GetDeltaTime(),
            fogCB.gFogStart,
            fogCB.gFogEnd
        );

        tiledLightData.lightCountSRV = tileLightList.lightCountSRV;
        tiledLightData.lightIndexSRV = tileLightList.lightIndexSRV;
        tiledLightData.lightCB =
            m_lightShadowResourceService->GetLightDataCB().Get();

        tiledLightData.tileCB =
            m_deferredRenderingService->GetTileCB().Get();

        m_rainService->SpawnDrawParticles(
            deviceContext,
            &tiledLightData
        );
    }
}