
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
	Context Application::ctx_{};
	std::unique_ptr<SFW::SimpleThreadPool> Application::threadPool_{};
	GraphicsDeviceType Application::graphics_{};
	TerrainBoot::Result Application::terrainRes_{};
    TerrainWater Application::terrainWater_{};
    std::unique_ptr<Graphics::I3DPerCameraService> Application::perCameraService_{};
    std::unique_ptr<Graphics::LightShadowService> Application::lightShadowService_{};
    std::unique_ptr<WindService> Application::windService_{};
    std::unique_ptr<DeferredRenderingService> Application::deferredRenderingService_{};
    std::unique_ptr<Graphics::DX11::LightShadowResourceService> Application::lightShadowResourceService_{};
	std::unique_ptr<EnvironmentService> Application::environmentService_{};
	std::unique_ptr<FireflyService> Application::fireflyService_{};
	std::unique_ptr<LeafService> Application::leafService_{};
    std::unique_ptr<RainService> Application::rainService_{};
	std::unique_ptr<SFW::TimerService> Application::timerService_{};
	std::unique_ptr<GameEngineType> Application::gameEngine_{};
	ComPtr<ID3D11SamplerState> Application::linearSampler_{};
	ComPtr<ID3D11SamplerState> Application::pointSampler_{};
	ComPtr<ID3D11ShaderResourceView> Application::leafTextureSRV_{};
	Graphics::HeightTexMapping Application::heightTexMap_{};


    bool Application::Initialize()
    {
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

        threadPool_ = std::make_unique<SFW::SimpleThreadPool>();

        return true;
    }

    bool Application::InitializeWindow()
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);
        return true;
    }

    bool Application::InitializeGraphics()
    {
        graphics_.Configure<SFW::Debug::ImGuiBackendDX11Win32>(
            WindowHandler::GetMainHandle(),
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            FPS_LIMIT
        );

        return true;
    }

    bool Application::InitializeTerrain()
    {
        terrainRes_ = TerrainBoot::BuildAll(graphics_, terrainRank_);

        heightTexMap_ =
            Graphics::MakeHeightTexMappingFromTerrainParams(
                terrainRes_.params,
                terrainRes_.heightMap
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
            (terrainRes_.params.cellsX + 1) * terrainRes_.params.cellSize;

        param.worldMapSizeZ =
            (terrainRes_.params.cellsZ + 1) * terrainRes_.params.cellSize;

        param.worldOffset = terrainRes_.params.offset;
        param.heightScale = terrainRes_.params.heightScale;
        param.clusterCellsX = 16;
        param.clusterCellsZ = 16;
        param.cellSize = 6.0f;

        terrainWater_.BuildCluster(param);

        terrainWater_.CompileShader(
            graphics_.GetDevice(),
            L"assets/shader/CS_TerrainWater.cso",
            L"assets/shader/CS_WriteArgs.cso",
            L"assets/shader/VS_TerrainWater.cso",
            L"assets/shader/PS_TerrainWater.cso"
        );

        terrainWater_.CreateResource(graphics_);

        return true;
    }

    bool Application::InitializePhysics()
    {
        Physics::PhysicsDevice::InitParams params{};
        params.maxBodies = 100000;
        params.maxBodyPairs = 64 * 1024;
        params.maxContactConstraints = 2 * 1024;
        params.workerThreads = -1;

        if (!physics_.Initialize(params))
        {
            assert(false && "Failed Physics Device Initialize");
            return false;
        }

        Physics::PhysicsService::Plan physicsPlan =
        {
            1.0f / static_cast<float>(FPS_LIMIT),
            1,
            false
        };

        physicsService_ = std::make_unique<Physics::PhysicsService>(
            physics_,
            shapeManager_,
            physicsPlan,
            1u << 14
        );

        return true;
    }

    bool Application::InitializeServices()
    {
        auto renderService = graphics_.GetRenderService();

        auto bufferMgr =
            renderService->GetResourceManager<Graphics::DX11::BufferManager>();

        auto textureMgr =
            renderService->GetResourceManager<Graphics::DX11::TextureManager>();

        winInput_ = std::make_unique<Input::WinInput>(
            WindowHandler::GetMouseInput()
        );

        perCameraService_ =
            std::make_unique<Graphics::DX11::PerCamera3DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        perCameraService_->SetFarClip(1500.0f);

        ortCameraService_ =
            std::make_unique<Graphics::DX11::OrtCamera3DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        camera2DService_ =
            std::make_unique<Graphics::DX11::Camera2DService>(
                bufferMgr,
                WINDOW_WIDTH,
                WINDOW_HEIGHT
            );

        lightShadowService_ =
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

        lightShadowService_->SetCascadeConfig(cascadeConfig);

        windService_ = std::make_unique<WindService>(bufferMgr);

        playerService_ = std::make_unique<PlayerService>(bufferMgr);

        {
            const auto& tp = terrainRes_.params;

            Math::Vec3f playerLocation =
            {
                tp.cellsX * tp.cellSize * 0.26f,
                0.0f,
                tp.cellsZ * tp.cellSize * 0.19f
            };

            terrainRes_.terrain->SampleHeightNormalBilinear(
                playerLocation.x,
                playerLocation.z,
                playerLocation.y
            );

            playerLocation.y += 2.0f;
            playerService_->SetPlayerPosition(playerLocation);
        }

        audioService_ = std::make_unique<Audio::AudioService>();

        if (!audioService_->Initialize())
        {
            assert(false && "Failed Audio Service Initialize");
            return false;
        }

        auto device = graphics_.GetDevice();
        auto deviceContext = graphics_.GetDeviceContext();

        deferredRenderingService_ =
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

        lightShadowResourceService_ =
            std::make_unique<Graphics::DX11::LightShadowResourceService>();

        Graphics::DX11::ShadowMapConfig shadowMapConfig{};
        shadowMapConfig.width = SHADOW_MAP_SIZE;
        shadowMapConfig.height = SHADOW_MAP_SIZE;

        if (!lightShadowResourceService_->Initialize(device, shadowMapConfig))
        {
            assert(false && "Failed ShadowMapService Initialize");
            return false;
        }

        pointLightService_ =
            std::make_unique<Graphics::PointLightService>();

        environmentService_ =
            std::make_unique<EnvironmentService>(bufferMgr);

        spriteAnimationService_ =
            std::make_unique<SpriteAnimationService>(bufferMgr);

        fireflyService_ =
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

        leafService_ =
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

        rainService_ =
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

        timerService_ = std::make_unique<SFW::TimerService>();

        return true;
    }

    bool Application::InitializeParticleResources()
    {
        auto textureMgr =
            graphics_
            .GetRenderService()
            ->GetResourceManager<Graphics::DX11::TextureManager>();

        Graphics::DX11::TextureCreateDesc texDesc{};
        texDesc.path = "assets/texture/sprite/Leaf.png";
        texDesc.forceSRGB = true;

        Graphics::TextureHandle texHandle;
        textureMgr->Add(texDesc, texHandle);

        auto texData = textureMgr->Get(texHandle);
        leafTextureSRV_ = texData.ref().srv;

        return true;
    }

    bool App::Application::InitializeContext()
    {
        ctx_.graphics = &graphics_;
        ctx_.renderService = graphics_.GetRenderService();
        ctx_.shadowRes = lightShadowResourceService_.get();
        ctx_.pointLight = pointLightService_.get();
        ctx_.deferred = deferredRenderingService_.get();
        ctx_.wind = windService_.get();
        ctx_.player = playerService_.get();
        ctx_.env = environmentService_.get();
        ctx_.firefly = fireflyService_.get();
        ctx_.leaf = leafService_.get();
        ctx_.rain = rainService_.get();

        return true;
	}

    bool App::Application::InitializeRenderPipeline()
    {
        graphics_.ExecuteCustomFunc(
            [this](
                auto* rg,
                auto& mainRTV,
                auto& mainDSV,
                auto& mainDSVRO,
                auto& mainDepthSRV
                )
            {
                ctx_.mainDepthSRV = mainDepthSRV;

                RenderPipe::Initialize(
                    rg,
                    ctx_,
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
			graphics_.GetRenderService(),
			physicsService_.get(),
            winInput_.get(),
            perCameraService_.get(),
            ortCameraService_.get(),
            camera2DService_.get(),
            lightShadowService_.get(),
            windService_.get(),
            playerService_.get(),
            audioService_.get(),
            deferredRenderingService_.get(),
            lightShadowResourceService_.get(),
            environmentService_.get(),
			pointLightService_.get(),
            spriteAnimationService_.get(),
            fireflyService_.get(),
            leafService_.get(),
            rainService_.get(),
            timerService_.get()
        );

        locator.InitAndRegisterStaticService<SpatialChunkRegistry>();

		WorldType world(std::move(locator));

		gameEngine_ = std::make_unique<GameEngineType>(std::move(graphics_), std::move(world), App::FPS_LIMIT);

        return true;
    }

    void App::Application::Run()
    {
        WindowHandler::Run([]()
            {
                gameEngine_->MainLoop(threadPool_.get());
            });
    }

    int App::Application::Shutdown()
    {
        threadPool_.reset();
        return WindowHandler::Destroy();
    }


    void App::Application::RegisterRenderCallbacks()
    {
        auto renderService = graphics_.GetRenderService();

        renderService->SetCustomUpdateFunction(
            [](Graphics::RenderService* renderService)
            {
                bool execute = ctx_.executeCustom.load(std::memory_order_relaxed);
                if (!execute) return;

                auto viewProj = perCameraService_->GetCameraBufferDataNoLock().viewProj;
                auto camPos = perCameraService_->GetEyePos();

                auto resolution = perCameraService_->GetResolution();
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
                    *terrainRes_.terrain, heightTexMap_, hopt, opt, clusterIds, trisW, &trisC);

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
                bool execute = ctx_.executeCustom.load(std::memory_order_relaxed);
                if (!execute) return;

                auto deviceContext = graphics_.GetDeviceContext();

                //auto viewProj = perCameraService->GetCameraBufferData().viewProj;
                auto camPos = perCameraService_->GetEyePos();
                Math::Frustumf frustumPlanes
                    //Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
                    = perCameraService_->MakeFrustum(true);

                auto resolution = perCameraService_->GetResolution();
                uint32_t width = (uint32_t)resolution.x;
                uint32_t height = (uint32_t)resolution.y;

                graphics_.SetDepthStencilState(Graphics::DepthStencilStateID::Default);
                graphics_.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

                deviceContext->VSSetSamplers(3, 1, linearSampler_.GetAddressOf());

                static Graphics::DX11::BlockReservedContext::ShadowDepthParams shadowParams{};

                shadowParams.mainDSV = graphics_.GetMainDepthStencilView().Get();
                shadowParams.mainViewProj = perCameraService_->MakeViewProjMatrix();
                memcpy(shadowParams.mainFrustumPlanes, frustumPlanes.data(), sizeof(shadowParams.mainFrustumPlanes));
                auto& cascadeDSV = lightShadowResourceService_->GetCascadeDSV();
                for (int c = 0; c < Graphics::kMaxShadowCascades; ++c) {
                    shadowParams.cascadeDSV[c] = cascadeDSV[c].Get();
                }

                auto& cascade = lightShadowService_->GetCascades();
                memcpy(shadowParams.lightViewProj, cascade.lightViewProj.data(), sizeof(shadowParams.lightViewProj));
                shadowParams.cascadeFrustumPlanes = cascade.frustumWS;

                // フラスタムをライトの逆方向に押し出して影の判定を緩める
                auto pushDir = lightShadowService_->GetDirectionalLight().directionWS * -1.0f;
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

                lightShadowResourceService_->ClearDepthBuffer(deviceContext);

                //雨用の深度マップもクリアしておく
                rainService_->ClearDepthMap(deviceContext);

                //CBの5, Samplerの1にバインド
                lightShadowResourceService_->BindShadowResources(deviceContext, 5);

                auto bufMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
                auto cameraHandle = bufMgr->FindByName(Graphics::DX11::PerCamera3DService::BUFFER_NAME);
                ComPtr<ID3D11Buffer> cameraCB;
                {
                    auto cameraBufData = bufMgr->Get(cameraHandle);
                    cameraCB = cameraBufData->buffer;
                }

                terrainRes_.blockRevert->RunShadowDepth(deviceContext,
                    cameraCB,
                    terrainRes_.heightMapSRV,
                    terrainRes_.normalMapSRV,
                    shadowParams,
                    *terrainRes_.cp,
                    &lightShadowResourceService_->GetCascadeViewport(), false);

                // 地形の水面の可視インデックスを計算してバッファに書き込む
                TerrainWater::ComputeContext computeCtx{};
                computeCtx.devCtx = deviceContext;
                computeCtx.mainFrustum = frustumPlanes;
                computeCtx.viewProj = shadowParams.mainViewProj;
                computeCtx.screenSize[0] = App::WINDOW_WIDTH;
                computeCtx.screenSize[1] = App::WINDOW_HEIGHT;

                terrainWater_.ComputeVisibleIndices(computeCtx, cameraCB);

                // 雨用に上からの地形の遮蔽マップを描画しておく
                Graphics::DX11::TerrainPatchCB patchCBData{};
                patchCBData.gPatchCenterXZ = { camPos.x, camPos.z };
                patchCBData.gPatchHalfSize = rainService_->GetSpawnRadius();
                patchCBData.gPatchVertsX = 32;
                patchCBData.gPatchVertsZ = 32;

                terrainRes_.blockRevert->RunPatchDepth(deviceContext,
                    *terrainRes_.cp,
                    patchCBData,
                    rainService_->GetMatrixCB(),
                    terrainRes_.heightMapSRV,
                    rainService_->GetDepthMapDSV()
                );

                // 雨の湿り気マップを更新
                rainService_->UpdateWetnessCS(deviceContext);
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
            auto& worldRequestService = gameEngine_->GetWorld().GetRequestServiceNoLock();

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
            auto& worldRequestService = gameEngine_->GetWorld().GetRequestServiceNoLock();
            auto reqCmd = worldRequestService.CreateCleanLevelCommand(newLevelName);
            worldRequestService.PushCommand(std::move(reqCmd));
            });
    }

    void App::Application::LoadInitialLevel()
    {
		auto& world = gameEngine_->GetWorld();

        Levels::EnqueueGlobalSystems(world);
        Levels::EnqueueLoadingLevel(world, ctx_, App::LOADING_LEVEL_NAME);
        Levels::EnqueueTitleLevel(world, ctx_);
        static Levels::OpenFieldLevelParams openFieldParams =
        {
            .gridHandle = terrainRes_.cp->gridHandle,
            .heightTexHandle = terrainRes_.heightTexHandle,
            .terrainParams = terrainRes_.params,
            .terrainClustered = *terrainRes_.terrain,
            .cpuSplatImage = *terrainRes_.cpuSplatImage,
            .heightMap = terrainRes_.heightMap,
            .terrainRank = terrainRank_
        };

        Levels::EnqueueOpenFieldLevel(world, ctx_, openFieldParams);
        //初めのレベルをロード
        {
            world.LoadLevel("Title");
        }
    }

    void App::Application::DrawTerrainColor(uint64_t frame)
    {
        if (!ctx_.executeCustom.load(std::memory_order_relaxed))
            return;

        graphics_.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        graphics_.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

        auto deviceContext = graphics_.GetDeviceContext();

        deviceContext->VSSetSamplers(3, 1, linearSampler_.GetAddressOf());
        deviceContext->PSSetSamplers(3, 1, pointSampler_.GetAddressOf());

        Graphics::DX11::BindCommonMaterials(
            deviceContext,
            *terrainRes_.matRes
        );

        ID3D11ShaderResourceView* splatSrv =
            terrainRes_.splatRes->splatArraySRV.Get();

        deviceContext->PSSetShaderResources(28, 1, &splatSrv);

        ID3D11ShaderResourceView* biomeSrv =
            terrainRes_.splatRes->biomeArraySRV.Get();

        deviceContext->PSSetShaderResources(30, 1, &biomeSrv);

        deviceContext->RSSetViewports(1, &graphics_.GetMainViewport());

        terrainRes_.blockRevert->RunColor(
            deviceContext,
            terrainRes_.heightMapSRV,
            terrainRes_.normalMapSRV,
            *terrainRes_.cp
        );
    }

    void App::Application::DrawOpaqueParticle(uint64_t frame)
    {
        auto* deviceContext = graphics_.GetDeviceContext();

        graphics_.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        graphics_.SetBlendState(Graphics::BlendStateID::Additive);

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
                graphics_.GetMainDepthStencilViewReadOnly().Get();

            deviceContext->OMSetRenderTargets(
                DeferredTextureCount,
                rtvs,
                readOnlyDsv
            );
        }

        const uint32_t slot =
            static_cast<uint32_t>(frame % Graphics::RENDER_BUFFER_COUNT);

        // ホタルのスポーンと描画
        fireflyService_->SpawnDrawParticles(
            deviceContext,
            terrainRes_.heightMapSRV,
            terrainRes_.cp->cbGrid,
            slot
        );

        ComPtr<ID3D11Buffer> windCb;

        {
            auto windHandle = windService_->GetBufferHandle();

            auto windBufferData =
                graphics_
                .GetRenderService()
                ->GetResourceManager<Graphics::DX11::BufferManager>()
                ->Get(windHandle);

            windCb = windBufferData->buffer;
        }

        ComPtr<ID3D11ShaderResourceView> mainDepthSRV =
            graphics_.GetMainDepthStencilSRV();

        // 葉っぱのスポーン
        leafService_->SpawnDrawParticles(
            deviceContext,
            terrainRes_.heightMapSRV,
            mainDepthSRV,
            terrainRes_.cp->cbGrid,
            windCb,
            slot
        );

        // 葉っぱは不透明扱い
        graphics_.SetBlendState(Graphics::BlendStateID::Opaque);
        graphics_.SetDepthStencilState(Graphics::DepthStencilStateID::Default);

        deviceContext->OMSetRenderTargets(
            DeferredTextureCount,
            rtvs,
            graphics_.GetMainDepthStencilView().Get()
        );

        leafService_->DrawParticles(
            deviceContext,
            leafTextureSRV_
        );

        for (auto* rtv : rtvs)
        {
            if (rtv)
                rtv->Release();
        }
    }

    void App::Application::DrawTransparentParticle(uint64_t frame)
    {
        auto* deviceContext = graphics_.GetDeviceContext();

        graphics_.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
        graphics_.SetBlendState(Graphics::BlendStateID::Premultiplied);

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
                graphics_.GetMainDepthStencilViewReadOnly().Get();

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
            lightShadowResourceService_->GetPointLightSRV().Get();

        tiledLightData.fireflyLightSRV =
            fireflyService_->GetPointLightSRV();

        auto tileLightList =
            deferredRenderingService_->GetTileLightList(false);

        deviceContext->VSSetSamplers(
            3,
            1,
            linearSampler_.GetAddressOf()
        );

        auto fogCB = environmentService_->GetFogCBData();

        terrainWater_.Render(
            deviceContext,
            lightShadowResourceService_->GetLightDataCB(),
            graphics_.GetMainDepthStencilSRV(),
            Math::Inverse(perCameraService_->GetCameraBufferDataNoLock().proj),
            perCameraService_->GetEyePos(),
            timerService_->GetDeltaTime(),
            fogCB.gFogStart,
            fogCB.gFogEnd
        );

        tiledLightData.lightCountSRV = tileLightList.lightCountSRV;
        tiledLightData.lightIndexSRV = tileLightList.lightIndexSRV;
        tiledLightData.lightCB =
            lightShadowResourceService_->GetLightDataCB().Get();

        tiledLightData.tileCB =
            deferredRenderingService_->GetTileCB().Get();

        rainService_->SpawnDrawParticles(
            deviceContext,
            &tiledLightData
        );
    }
}