
/*****************************************************************//**
 * @file   main.cpp
 * @brief SectorX コンソールプロジェクトのエントリーポイント
 * @author seigo_t03b63m
 * @date   December 2025
 *********************************************************************/

//========================================================================
// 一人のプロジェクトなので、とりあえず初期化関連の処理をmainに書いています
// 将来的には適切に分割する
//========================================================================

//SectorFW
#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include <SectorFW/DX11WinTerrainHelper.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>
#include <SectorFW/Graphics/TerrainOccluderExtraction.h>
#include <SectorFW/Graphics/ImageLoader.h>
#include <SectorFW/Debug/UIBus.h>

#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "app/component_registration.h"
#include "graphics/RenderPipeline.h"
#include "terrain/TerrainBootstrap.h"
#include "level/LevelBuilders.h"

#include "app/PlayerService.h"
#include "graphics/DeferredRenderingService.h"
#include "graphics/SpriteAnimationService.h"
#include "environment/WindService.h"
#include "environment/EnvironmentService.h"
#include "environment/FireflyService.h"
#include "environment/LeafService.h"


int main(void)
{
	LOG_INFO("SectorX Console Project started");

	App::RegisterComponents();

	WindowHandler::Create(_T(WINDOW_NAME), App::WINDOW_WIDTH, App::WINDOW_HEIGHT);

	static SFW::Graphics::DX11::GraphicsDevice graphics;
	graphics.Configure<SFW::Debug::ImGuiBackendDX11Win32>(
		WindowHandler::GetMainHandle(),
		App::WINDOW_WIDTH, App::WINDOW_HEIGHT,
		App::FPS_LIMIT
	);

	// デバイス & サービス（Worldコンテナ）
	Physics::PhysicsDevice::InitParams params;
	params.maxBodies = 100000;
	params.maxBodyPairs = 64 * 1024;
	params.maxContactConstraints = 2 * 1024;
	params.workerThreads = -1; // 自動

	Physics::PhysicsDevice physics;
	bool ok = physics.Initialize(params);
	if (!ok) {
		assert(false && "Failed Physics Device Initialize");
	}

	Physics::PhysicsShapeManager shapeManager;
	Physics::PhysicsService::Plan physicsPlan = { 1.0f / (float)App::FPS_LIMIT, 1, false };
	Physics::PhysicsService physicsService(physics, shapeManager, physicsPlan);

	Input::WinInput winInput(WindowHandler::GetMouseInput());
	InputService* inputService = &winInput;

	auto bufferMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::BufferManager>();
	auto textureManager = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>();
	Graphics::DX11::PerCamera3DService dx11PerCameraService(bufferMgr, App::WINDOW_WIDTH, App::WINDOW_HEIGHT);
	static Graphics::I3DPerCameraService* perCameraService = &dx11PerCameraService;

	Graphics::DX11::OrtCamera3DService dx11OrtCameraService(bufferMgr, App::WINDOW_WIDTH, App::WINDOW_HEIGHT);
	Graphics::I3DOrtCameraService* ortCameraService = &dx11OrtCameraService;

	Graphics::DX11::Camera2DService dx112DCameraService(bufferMgr, App::WINDOW_WIDTH, App::WINDOW_HEIGHT);
	Graphics::I2DCameraService* camera2DService = &dx112DCameraService;

	auto device = graphics.GetDevice();
	auto deviceContext = graphics.GetDeviceContext();

	auto renderService = graphics.GetRenderService();

	static Graphics::LightShadowService lightShadowService;
	Graphics::LightShadowService::CascadeConfig cascadeConfig;
	cascadeConfig.shadowMapResolution = Math::Vec2f(float(App::SHADOW_MAP_SIZE), float(App::SHADOW_MAP_SIZE));
	cascadeConfig.cascadeCount = 3;
	cascadeConfig.shadowDistance = 80.0f;
	cascadeConfig.casterExtrusion = 0.0f;
	lightShadowService.SetCascadeConfig(cascadeConfig);

	static WindService windService(bufferMgr);

	PlayerService playerService(bufferMgr);

	Audio::AudioService audioService;
	ok = audioService.Initialize();
	assert(ok && "Failed Audio Service Initialize");

	DeferredRenderingService deferredRenderingService(bufferMgr, textureManager, App::WINDOW_WIDTH, App::WINDOW_HEIGHT);

	static Graphics::DX11::LightShadowResourceService lightShadowResourceService;
	Graphics::DX11::ShadowMapConfig shadowMapConfig;
	shadowMapConfig.width = App::SHADOW_MAP_SIZE;
	shadowMapConfig.height = App::SHADOW_MAP_SIZE;
	ok = lightShadowResourceService.Initialize(device, shadowMapConfig);
	assert(ok && "Failed ShadowMapService Initialize");

	static Graphics::PointLightService pointLightService;

	EnvironmentService environmentService(bufferMgr);

	static SpriteAnimationService spriteAnimationService(bufferMgr);

	//地形のコンピュートと同じタイミングで描画する
	static FireflyService fireflyService(device, deviceContext, bufferMgr,
		L"assets/shader/CS_ParticleInitFreeList.cso",
		L"assets/shader/CS_FireflySpawn.cso",
		L"assets/shader/CS_FireflyUpdate.cso",
		L"assets/shader/CS_ParticleArgs.cso",
		L"assets/shader/VS_FireflyBillboard.cso",
		L"assets/shader/PS_Firefly.cso");

	static LeafService leafService(device, deviceContext, bufferMgr,
		L"assets/shader/CS_ParticleInitFreeList.cso",
		L"assets/shader/CS_LeafClumpUpdate.cso",
		L"assets/shader/CS_LeafSpawn.cso",
		L"assets/shader/CS_LeafUpdate.cso",
		L"assets/shader/CS_ParticleArgs.cso",
		L"assets/shader/VS_LeafBillboard.cso",
		L"assets/shader/PS_Leaf.cso");

	ECS::ServiceLocator serviceLocator(
		renderService, &physicsService, inputService, perCameraService,
		ortCameraService, camera2DService, &lightShadowService, &windService,
		&playerService, &audioService, &deferredRenderingService, &lightShadowResourceService,
		&pointLightService, &environmentService, &spriteAnimationService, &fireflyService, &leafService);

	serviceLocator.InitAndRegisterStaticService<SpatialChunkRegistry, TimerService>();


	static App::Context ctx;
	ctx.graphics = &graphics;
	ctx.renderService = graphics.GetRenderService();
	ctx.shadowRes = &lightShadowResourceService;
	ctx.deferred = &deferredRenderingService;
	ctx.wind = &windService;
	ctx.player = &playerService;
	ctx.env = &environmentService;
	ctx.firefly = &fireflyService;
	ctx.leaf = &leafService;


	enum class TerrainRank : int {
		Low = 1,
		Middle = 2,
		High = 4
	};

	int terrainRank = (int)TerrainRank::High;

	// Terrain 構築
	static auto terrainRes = TerrainBoot::BuildAll(graphics, terrainRank);

	static ComPtr<ID3D11SamplerState> linearSampler;
	{
		using namespace Graphics;
		auto samplerManager = graphics.GetRenderService()->GetResourceManager<DX11::SamplerManager>();

		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		SamplerHandle samp = samplerManager->AddWithDesc(sampDesc);
		auto sampData = samplerManager->Get(samp);
		linearSampler = sampData.ref().state;
	}

	static ComPtr<ID3D11SamplerState> pointSampler;
	{
		using namespace Graphics;
		auto samplerManager = graphics.GetRenderService()->GetResourceManager<DX11::SamplerManager>();
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerHandle samp = samplerManager->AddWithDesc(sampDesc);
		auto sampData = samplerManager->Get(samp);
		pointSampler = sampData.ref().state;
	}

	// ---- マッピング設定(オクルージョンカリング用) ----
	static Graphics::HeightTexMapping heightTexMap = Graphics::MakeHeightTexMappingFromTerrainParams(terrainRes.params, terrainRes.heightMap);

	auto terrainUpdateFunc = [](Graphics::RenderService* renderService)
		{
			bool execute = ctx.executeCustom.load(std::memory_order_relaxed);
			if (!execute) return;

			auto viewProj = perCameraService->GetCameraBufferDataNoLock().viewProj;
			auto camPos = perCameraService->GetEyePos();

			auto resolution = perCameraService->GetResolution();
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
				*terrainRes.terrain, heightTexMap, hopt, opt, clusterIds, trisW, &trisC);

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
		};

	auto PreDrawFunc = [](Graphics::RenderService* renderService, uint32_t slot)
		{
			bool execute = ctx.executeCustom.load(std::memory_order_relaxed);
			if (!execute) return;

			auto deviceContext = graphics.GetDeviceContext();

			//auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();
			Math::Frustumf frustumPlanes
				//Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
				= perCameraService->MakeFrustum(true);

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::Default);
			graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

			deviceContext->VSSetSamplers(3, 1, linearSampler.GetAddressOf());

			static Graphics::DX11::BlockReservedContext::ShadowDepthParams shadowParams{};

			shadowParams.mainDSV = graphics.GetMainDepthStencilView().Get();
			shadowParams.mainViewProj = perCameraService->MakeViewProjMatrix();
			memcpy(shadowParams.mainFrustumPlanes, frustumPlanes.data(), sizeof(shadowParams.mainFrustumPlanes));
			auto& cascadeDSV = lightShadowResourceService.GetCascadeDSV();
			for (int c = 0; c < Graphics::kMaxShadowCascades; ++c) {
				shadowParams.cascadeDSV[c] = cascadeDSV[c].Get();
			}

			auto& cascade = lightShadowService.GetCascades();
			memcpy(shadowParams.lightViewProj, cascade.lightViewProj.data(), sizeof(shadowParams.lightViewProj));
			shadowParams.cascadeFrustumPlanes = cascade.frustumWS;

			// フラスタムをライトの逆方向に押し出して影の判定を緩める
			auto pushDir = lightShadowService.GetDirectionalLight().directionWS * -1.0f;
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

			lightShadowResourceService.ClearDepthBuffer(deviceContext);

			//CBの5, Samplerの1にバインド
			lightShadowResourceService.BindShadowResources(deviceContext, 5);

			auto bufMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
			auto cameraHandle = bufMgr->FindByName(Graphics::DX11::PerCamera3DService::BUFFER_NAME);
			ComPtr<ID3D11Buffer> cameraCB;
			{
				auto cameraBufData = bufMgr->Get(cameraHandle);
				cameraCB = cameraBufData->buffer;
			}

			terrainRes.blockRevert->RunShadowDepth(deviceContext,
				std::move(cameraCB),
				terrainRes.heightMapSRV,
				terrainRes.normalMapSRV,
				shadowParams,
				*terrainRes.cp,
				&lightShadowResourceService.GetCascadeViewport(), false);
		};

	auto drawTerrainColor = [](uint64_t frame)
		{
			bool execute = ctx.executeCustom.load(std::memory_order_relaxed);
			if (!execute) return;

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
			graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

			auto deviceContext = graphics.GetDeviceContext();

			deviceContext->VSSetSamplers(3, 1, linearSampler.GetAddressOf());

			//重みテクスチャをポイントサンプラーで参照
			deviceContext->PSSetSamplers(3, 1, pointSampler.GetAddressOf());

			// フレームの先頭 or Terrainパスの先頭で 1回だけ：
			Graphics::DX11::BindCommonMaterials(deviceContext, *terrainRes.matRes);

			ID3D11ShaderResourceView* splatSrv = terrainRes.splatRes->splatArraySRV.Get();
			deviceContext->PSSetShaderResources(24, 1, &splatSrv);       // t24

			deviceContext->RSSetViewports(1, &graphics.GetMainViewport());

			terrainRes.blockRevert->RunColor(deviceContext, terrainRes.heightMapSRV, terrainRes.normalMapSRV, *terrainRes.cp);
		};

	static ComPtr<ID3D11ShaderResourceView> leafTextureSRV;
	{
		auto textureMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>();
		Graphics::DX11::TextureCreateDesc texDesc;
		texDesc.path = "assets/texture/sprite/Leaf.png";
		texDesc.forceSRGB = true;
		Graphics::TextureHandle texHandle;
		textureMgr->Add(texDesc, texHandle);
		auto texData = textureMgr->Get(texHandle);
		leafTextureSRV = texData.ref().srv;
	}

	auto drawParticle = [](uint64_t frame)
		{
			auto deviceContext = graphics.GetDeviceContext();

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
			graphics.SetBlendState(Graphics::BlendStateID::Additive);

			//とりあえず安全にRTVをキャプチャして維持したままDSVだけ差し替え
			// ※本来はRTVをキャッシュしておいて差し替えた方が軽量
			{
				ID3D11RenderTargetView* rtvs[DeferredTextureCount] = {};
				ID3D11DepthStencilView* curDsv = nullptr;

				deviceContext->OMGetRenderTargets(DeferredTextureCount, rtvs, &curDsv);

				// ここで curDsv は使わないなら Release する
				if (curDsv) curDsv->Release();

				// 目的の DSV に差し替える
				ID3D11DepthStencilView* newDsv = graphics.GetMainDepthStencilViewReadOnly().Get();

				// RTV を維持したまま DSV だけ変更
				deviceContext->OMSetRenderTargets(DeferredTextureCount, rtvs, newDsv);

				// OMGetRenderTargets は AddRef して返すので Release 必須
				for (auto& rtv : rtvs) if (rtv) rtv->Release();
			}

			uint32_t slot = frame % Graphics::RENDER_BUFFER_COUNT;

			//ホタルのスポーンと描画処理
			fireflyService.SpawnParticles(deviceContext, terrainRes.heightMapSRV, terrainRes.cp->cbGrid, slot);

			// 葉っぱは不透明(ピクセルでアルファを抜く)
			graphics.SetBlendState(Graphics::BlendStateID::AlphaBlend);

			ComPtr<ID3D11Buffer> windCb;
			{
				auto windHandle = windService.GetBufferHandle();

				auto windBufData = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::BufferManager>()->Get(windHandle);
				windCb = windBufData->buffer;
			}


			ComPtr<ID3D11ShaderResourceView> mainDepthSRV = graphics.GetMainDepthStencilSRV();

			//葉っぱのスポーンと描画処理
			leafService.SpawnParticles(deviceContext, terrainRes.heightMapSRV, leafTextureSRV, mainDepthSRV, terrainRes.cp->cbGrid, windCb, slot);
		};

	renderService->SetCustomUpdateFunction(terrainUpdateFunc);
	renderService->SetCustomPreDrawFunction(PreDrawFunc);

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SFW::Graphics;

	// RenderGraph 初期化（ExecuteCustomFunc 内）
	graphics.ExecuteCustomFunc([&](auto* rg, auto& mainRTV, auto& mainDSV, auto& mainDSVRO, auto& mainDepthSRV)
		{
			ctx.mainDepthSRV = mainDepthSRV;
			RenderPipe::Initialize(rg, ctx, mainRTV, mainDSV, mainDSVRO, mainDepthSRV, drawTerrainColor, drawParticle);
		});

	constexpr const char* LOADING_LEVEL_NAME = "Loading";

	// World 構築 + level enqueue
	WorldType world(std::move(serviceLocator));

	Levels::EnqueueGlobalSystems(world);
	Levels::EnqueueLoadingLevel(world, ctx, LOADING_LEVEL_NAME);
	Levels::EnqueueTitleLevel(world, ctx);

	Levels::OpenFieldLevelParams openFieldParams =
	{
		.gridHandle = terrainRes.cp->gridHandle,
		.heightTexHandle = terrainRes.heightTexHandle,
		.terrainParams = terrainRes.params,
		.terrainClustered = *terrainRes.terrain,
		.cpuSplatImage = *terrainRes.cpuSplatImage,
		.heightMap = terrainRes.heightMap,
		.terrainRank = terrainRank
	};

	Levels::EnqueueOpenFieldLevel(world, ctx, openFieldParams);

	//初めのレベルをロード
	{
		//ローディング中のレベルを先にロード
		world.LoadLevel(LOADING_LEVEL_NAME);

		//ロード完了後のコールバック
		auto loadedFunc = [](decltype(world)::Session* pSession) {

			//ローディングレベルをクリーンアップ
			pSession->CleanLevel(LOADING_LEVEL_NAME);
			};

		world.LoadLevel("Title", true, true, loadedFunc);
	}

	static GameEngine gameEngine(std::move(graphics), std::move(world), App::FPS_LIMIT);

	//シーンロードのデバッグコールバック登録
	{
		static std::string newLevelName;

		BIND_DEBUG_TEXT("Level", "Name", &newLevelName);

		static bool loadAsync = false;

		BIND_DEBUG_CHECKBOX("Level", "loadAsync", &loadAsync);

		REGISTER_DEBUG_BUTTON("Level", "load", [](bool) {
			auto& worldRequestService = gameEngine.GetWorld().GetRequestServiceNoLock();

			if (loadAsync) {
				//ローディング中のレベルを先にロード
				auto loadingCmd = worldRequestService.CreateLoadLevelCommand(LOADING_LEVEL_NAME, false);
				worldRequestService.PushCommand(std::move(loadingCmd));
			}

			//ロード完了後のコールバック
			auto loadedFunc = [](decltype(world)::Session* pSession) {

				//ローディングレベルをクリーンアップ
				pSession->CleanLevel(LOADING_LEVEL_NAME);
				};

			auto reqCmd = worldRequestService.CreateLoadLevelCommand(newLevelName, loadAsync, true, loadAsync ? loadedFunc : nullptr);
			worldRequestService.PushCommand(std::move(reqCmd));
			});

		REGISTER_DEBUG_BUTTON("Level", "clean", [](bool) {
			auto& worldRequestService = gameEngine.GetWorld().GetRequestServiceNoLock();
			auto reqCmd = worldRequestService.CreateCleanLevelCommand(newLevelName);
			worldRequestService.PushCommand(std::move(reqCmd));
			});
	}

	//スレッドプールクラス
	static std::unique_ptr<SimpleThreadPool> threadPool = std::make_unique<SimpleThreadPool>();

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop(threadPool.get());
		});

	// ワーカースレッドを停止
	threadPool.reset();

	return WindowHandler::Destroy();
}