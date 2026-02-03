#include "LevelBuilders.h"
#include "app/AppContext.h"
#include "app/appconfig.h"

//System
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/DebugRenderSystem.h"
#include "system/GlobalDebugRenderSystem.h"
#include "system/CleanModelSystem.h"
#include "system/SimpleModelRenderSystem.h"
#include "system/SpriteRenderSystem.h"
#include "system/PlayerSystem.h"
#include "system/EnvironmentSystem.h"
#include "system/DeferredRenderingSystem.h"
#include "system/LightShadowSystem.h"
#include "system/PointLightSystem.h"
#include "system/SpriteAnimationSystem.h"
#include "system/FireflySystem.h"
#include "system/LeafSystem.h"
#include "system/TitleSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"

#include "graphics/SpriteAnimationService.h"

constexpr float START_CAMERA_PLAYER_DISTANCE = 20.0f;


void Levels::EnqueueGlobalSystems(WorldType& world)
{
	auto& worldRequestService = world.GetRequestServiceNoLock();

	std::vector<std::unique_ptr<WorldType::IRequestCommand>> reqCmds;
	reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<CameraSystem>());
	reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<EnvironmentSystem>());
	reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<LightShadowSystem>());

#ifdef _ENABLE_IMGUI
	reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<GlobalDebugRenderSystem>());
#endif

	// レベル追加コマンドを実行キューにプッシュ
	for (auto& cmd : reqCmds) {
		worldRequestService.PushCommand(std::move(cmd));
	}
}

void Levels::EnqueueTitleLevel(WorldType& world, App::Context& ctx)
{
	using namespace SFW::Graphics;

	auto& worldRequestService = world.GetRequestServiceNoLock();
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	auto level = std::unique_ptr<Level<VoidPartition>>(new Level<VoidPartition>("Title", *entityManagerReg, ELevelState::Main));

	auto& graphics = *ctx.graphics;

	auto reqCmd = worldRequestService.CreateAddLevelCommand(std::move(level),
		[&](const ECS::ServiceLocator* serviceLocator, SFW::Level<VoidPartition>* pLevel)
		{
			auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
			auto matMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::MaterialManager>();
			auto shaderMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::ShaderManager>();
			auto psoMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::PSOManager>();
			auto sampMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::SamplerManager>();


			DX11::ShaderCreateDesc shaderDesc;
			shaderDesc.vsPath = L"assets/shader/VS_WindSprite.cso";
			shaderDesc.psPath = L"assets/shader/PS_Color.cso";
			ShaderHandle shaderHandle;
			shaderMgr->Add(shaderDesc, shaderHandle);

			DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
			PSOHandle psoHandle;
			psoMgr->Add(psoDesc, psoHandle);

			shaderDesc.vsPath = L"assets/shader/VS_ClipUVColor.cso";
			shaderDesc.psPath = L"assets/shader/PS_CircleAlpha.cso";
			shaderMgr->Add(shaderDesc, shaderHandle);

			psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
			PSOHandle alphaPsoHandle;
			psoMgr->Add(psoDesc, alphaPsoHandle);

			D3D11_SAMPLER_DESC sampDesc = {};
			sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			SamplerHandle samp = sampMgr->AddWithDesc(sampDesc);

			DX11::TextureCreateDesc textureDesc;
			textureDesc.path = "assets/texture/sprite/TitleText.png";
			textureDesc.forceSRGB = true;
			Graphics::TextureHandle texHandle;
			textureMgr->Add(textureDesc, texHandle);
			Graphics::DX11::MaterialCreateDesc matDesc;

			auto windCBHandle = ctx.wind->GetBufferHandle();

			matDesc.shader = shaderHandle;
			matDesc.samplerMap[0] = samp;
			matDesc.vsCBV[11] = windCBHandle; // VS_CB11 にセット
			matDesc.psSRV[2] = texHandle; // TEX2 にセット

			Graphics::MaterialHandle matHandle;
			matMgr->Add(matDesc, matHandle);
			CSprite sprite;
			sprite.hMat = matHandle;
			sprite.pso = psoHandle;
			auto levelSession = pLevel->GetSession();

			auto getPos = [](float x, float y)->Math::Vec3f {
				Math::Vec3f pos;
				pos.x = (App::WINDOW_WIDTH * x) / 2.0f;
				pos.y = (App::WINDOW_HEIGHT * y) / 2.0f;
				pos.z = 0.0f;
				return pos;
				};

			auto getScale = [](float x, float y)->Math::Vec3f {
				Math::Vec3f scale;
				scale.x = App::WINDOW_WIDTH * x;
				scale.y = App::WINDOW_HEIGHT * y;
				scale.z = 1.0f;
				return scale;
				};

			CColor colorWhite = { { 1.0f,1.0f,1.0f,1.0f} };
			CTitleSprite titleComp;

			sprite.layer = 1; // 手前に描画

			levelSession.AddGlobalEntity(
				CTransform{ getPos(0.0f,0.4f),{0.0f,0.0f,0.0f,1.0f}, getScale(0.7f,0.7f) },
				sprite,
				colorWhite,
				titleComp);

			textureDesc.path = "assets/texture/sprite/PressEnter.png";
			textureMgr->Add(textureDesc, texHandle);
			matDesc.psSRV[2] = texHandle; // TEX2 にセット
			matMgr->Add(matDesc, matHandle);
			sprite.hMat = matHandle;
			sprite.layer = 1; // 手前に描画
			titleComp.fadeTime = 2.5f;

			levelSession.AddGlobalEntity(
				CTransform{ getPos(0.0f,-0.7f),{0.0f,0.0f,0.0f,1.0f}, getScale(0.25f,0.25f) },
				sprite,
				colorWhite,
				titleComp);

			CColor colorBlack = { { 0.0f,0.0f,0.0f,1.0f} };

			sprite.hMat.index = CSprite::invalidIndex; // マテリアル無効化(真っ白マテリアルで描画）
			sprite.pso = alphaPsoHandle;
			sprite.layer = 2; // 一番前に描画
			titleComp.fadeTime = 2.0f;
			titleComp.isErased = true;

			levelSession.AddGlobalEntity(
				CTransform{ getPos(0.0f,0.0f),{0.0f,0.0f,0.0f,1.0f}, getScale(1.0f,1.0f) },
				sprite,
				colorBlack,
				titleComp);

			auto perCameraService = serviceLocator->Get<Graphics::I3DPerCameraService>();
			auto playerService = serviceLocator->Get<PlayerService>();

			auto pp = playerService->GetPlayerPosition();

			auto camRot = perCameraService->GetRotation();
			Math::Vec3f r, u, f;
			Math::ToBasis<float, Math::LH_ZForward>(camRot, r, u, f);

			perCameraService->SetTarget(pp - f * START_CAMERA_PLAYER_DISTANCE + Math::Vec3f{ 0.0f, 4.0f, 0.0f });
			Math::Quatf rot = Math::Quatf::FromAxisAngle({ 1.0f,0.0f,0.0f }, Math::Deg2Rad(-20.0f));
			perCameraService->Rotate(rot);

			auto& scheduler = pLevel->GetScheduler();
			scheduler.AddSystem<TitleSystem>(*serviceLocator);
			scheduler.AddSystem<SpriteRenderSystem>(*serviceLocator);

		});

	// レベル追加コマンドを実行キューにプッシュ
	worldRequestService.PushCommand(std::move(reqCmd));
}

void Levels::EnqueueLoadingLevel(WorldType& world, App::Context& ctx, const char* loadingName)
{
	using namespace SFW::Graphics;

	auto& worldRequestService = world.GetRequestServiceNoLock();
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	auto level = std::unique_ptr<Level<VoidPartition>>(new Level<VoidPartition>(loadingName, *entityManagerReg, ELevelState::Main));

	auto& graphics = *ctx.graphics;

	auto reqCmd = worldRequestService.CreateAddLevelCommand(std::move(level),
		[&](const ECS::ServiceLocator* serviceLocator, SFW::Level<VoidPartition>* pLevel)
		{
			auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
			auto matMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::MaterialManager>();
			auto shaderMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::ShaderManager>();
			auto sampMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::SamplerManager>();

			auto spriteAnimationService = serviceLocator->Get<SpriteAnimationService>();

			DX11::ShaderCreateDesc shaderDesc;
			shaderDesc.vsPath = L"assets/shader/VS_SpriteAnimation.cso";
			shaderDesc.psPath = L"assets/shader/PS_Color.cso";
			ShaderHandle shaderHandle;
			shaderMgr->Add(shaderDesc, shaderHandle);

			D3D11_SAMPLER_DESC sampDesc = {};
			sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			SamplerHandle samp = sampMgr->AddWithDesc(sampDesc);

			DX11::TextureCreateDesc textureDesc;
			textureDesc.path = "assets/texture/sprite/ToxicFrogPurpleBlue_Hop.png";
			textureDesc.forceSRGB = true;
			Graphics::TextureHandle texHandle;
			textureMgr->Add(textureDesc, texHandle);

			auto spriteInstBufferHandle = spriteAnimationService->GetInstanceBufferHandle();

			Graphics::DX11::MaterialCreateDesc matDesc;
			matDesc.shader = shaderHandle;
			matDesc.samplerMap[0] = samp;
			matDesc.vsSRV[11] = spriteInstBufferHandle; // VS_CB11 にセット
			matDesc.psSRV[2] = texHandle; // TEX2 にセット

			Graphics::MaterialHandle matHandle;
			matMgr->Add(matDesc, matHandle);

			CSpriteAnimation spriteAnim;
			spriteAnim.hMat = matHandle;
			spriteAnim.buf.divX = 7; // 横分割数
			spriteAnim.layer = 100; // 手前に描画

			auto getPos = [](float x, float y)->Math::Vec3f {
				Math::Vec3f pos;
				pos.x = (App::WINDOW_WIDTH * x) / 2.0f;
				pos.y = (App::WINDOW_HEIGHT * y) / 2.0f;
				pos.z = 0.0f;
				return pos;
				};

			auto getScale = [](float s)->Math::Vec3f {
				Math::Vec3f scale;
				constexpr auto half = (App::WINDOW_WIDTH + App::WINDOW_HEIGHT) / 2.0f;

				scale.x = half * s;
				scale.y = half * s;
				scale.z = 1.0f;
				return scale;
				};

			CColor color = { { 1.0f,1.0f,1.0f,1.0f} };

			auto levelSession = pLevel->GetSession();
			levelSession.AddGlobalEntity(
				CTransform{ getPos(0.9f, -0.85f), {0.0f,0.0f,0.0f,1.0f}, getScale(0.15f) },
				spriteAnim,
				color);

			auto& scheduler = pLevel->GetScheduler();
			scheduler.AddSystem<SpriteAnimationSystem>(*serviceLocator);

		});

	// レベル追加コマンドを実行キューにプッシュ
	worldRequestService.PushCommand(std::move(reqCmd));
}

void Levels::EnqueueOpenFieldLevel(WorldType& world, App::Context& ctx, const OpenFieldLevelParams& params)
{
	using OpenFieldLevel = SFW::Level<Grid2DPartition>;

	using namespace SFW::Graphics;

	auto& worldRequestService = world.GetRequestServiceNoLock();
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	auto level = std::unique_ptr<OpenFieldLevel>(new OpenFieldLevel(App::MAIN_LEVEL_NAME, *entityManagerReg, ELevelState::Main));

	auto& graphics = *ctx.graphics;

	auto reqCmd = worldRequestService.CreateAddLevelCommand(
		std::move(level),
		//ロード時
		[&](const ECS::ServiceLocator* serviceLocator, OpenFieldLevel* pLevel) {

			auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11::ModelAssetManager>();
			auto bufferMgr = graphics.GetRenderService()->GetResourceManager<DX11::BufferManager>();
			auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11::ShaderManager>();

			clock_t start = clock();


			//デフォルト描画のPSO生成
			DX11::ShaderCreateDesc shaderDesc;
			shaderDesc.templateID = MaterialTemplateID::PBR;
			shaderDesc.vsPath = L"assets/shader/VS_ClipUVNrm.cso";
			shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
			ShaderHandle shaderHandle;
			shaderMgr->Add(shaderDesc, shaderHandle);

			auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11::PSOManager>();
			DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
			PSOHandle cullDefaultPSOHandle;
			psoMgr->Add(psoDesc, cullDefaultPSOHandle);

			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
			PSOHandle cullNonePSOHandle;
			psoMgr->Add(psoDesc, cullNonePSOHandle);

			//草の揺れ用PSO生成
			shaderDesc.vsPath = L"assets/shader/VS_WindGrass.cso";
			shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
			shaderMgr->Add(shaderDesc, shaderHandle);
			PSOHandle windGrassPSOHandle;
			psoDesc.shader = shaderHandle;
			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
			psoMgr->Add(psoDesc, windGrassPSOHandle);
			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

			shaderDesc.vsPath = L"assets/shader/VS_WindEntity.cso";
			shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
			shaderMgr->Add(shaderDesc, shaderHandle);
			PSOHandle cullNoneWindEntityPSOHandle;
			psoDesc.shader = shaderHandle;

			shaderDesc.vsPath = L"assets/shader/VS_WindEntityShadow.cso";
			shaderDesc.psPath.clear();// 頂点シェーダのみ
			shaderMgr->Add(shaderDesc, shaderHandle);
			psoDesc.rebindShader = shaderHandle;

			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
			psoMgr->Add(psoDesc, cullNoneWindEntityPSOHandle);
			psoDesc.rebindShader = std::nullopt;
			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

			shaderDesc.vsPath = L"assets/shader/VS_NormalMap.cso";
			shaderDesc.psPath = L"assets/shader/PS_NormalMap.cso";
			shaderMgr->Add(shaderDesc, shaderHandle);
			PSOHandle normalMapPSOHandle;
			psoDesc.shader = shaderHandle;
			psoMgr->Add(psoDesc, normalMapPSOHandle);

			ModelAssetHandle modelAssetHandle[5];

			auto windCBHandle = ctx.wind->GetBufferHandle();
			auto footCBHandle = ctx.player->GetFootBufferHandle();

			auto materialMgr = graphics.GetRenderService()->GetResourceManager<DX11::MaterialManager>();
			// モデルアセットの読み込み
			DX11::ModelAssetCreateDesc modelDesc;
			modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_1.gltf";
			modelDesc.pso = cullDefaultPSOHandle;
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
			modelDesc.instancesPeak = 1000;
			modelDesc.viewMax = 100.0f;
			modelDesc.buildOccluders = false;

			modelAssetMgr->Add(modelDesc, modelAssetHandle[0]);

			modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
			modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

			modelDesc.path = "assets/model/Stylized/Tree01.gltf";
			modelDesc.viewMax = 50.0f;
			modelDesc.buildOccluders = false;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.minAreaFrec = 0.001f;
			modelDesc.pCustomNrmWFunc = WindService::ComputeTreeWeight;
			modelAssetMgr->Add(modelDesc, modelAssetHandle[1]);

			modelDesc.path = "assets/model/Stylized/YellowFlower.gltf";
			modelDesc.buildOccluders = false;
			modelDesc.viewMax = 50.0f;
			modelDesc.minAreaFrec = 0.0004f;
			modelDesc.pCustomNrmWFunc = WindService::ComputeGrassWeight;
			modelDesc.pso = cullNoneWindEntityPSOHandle;

			modelAssetMgr->Add(modelDesc, modelAssetHandle[2]);

			modelDesc.instancesPeak = 100;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.pCustomNrmWFunc = WindService::ComputeGrassWeight;
			modelDesc.minAreaFrec = 0.0004f;
			modelDesc.path = "assets/model/Stylized/WhiteCosmos.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[3]);

			modelDesc.instancesPeak = 100;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.path = "assets/model/Stylized/YellowCosmos.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[4]);
			modelDesc.ClearAdditionalBindings();

			ModelAssetHandle playerModelHandle;
			modelDesc.pso = cullDefaultPSOHandle;
			modelDesc.path = "assets/model/BlackGhost.glb";
			modelDesc.pCustomNrmWFunc = nullptr;
			modelDesc.minAreaFrec = 0.001f;
			modelAssetMgr->Add(modelDesc, playerModelHandle);

			ModelAssetHandle grassModelHandle;

			//ディファ―ド用のカメラの定数バッファハンドル取得
			auto deferredCameraHandle = bufferMgr->FindByName(DeferredRenderingService::BUFFER_NAME);

			modelDesc.BindVS_CBV("CameraBuffer", deferredCameraHandle); // カメラCBVをバインド
			modelDesc.BindVS_CBV("TerrainGridCB", params.gridHandle); // 地形グリッドCBVをバインド
			modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
			modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

			modelDesc.BindVS_SRV("gHeightMap", params.heightTexHandle); // 高さテクスチャをバインド

			modelDesc.instancesPeak = 10000;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = windGrassPSOHandle;
			modelDesc.pCustomNrmWFunc = WindService::ComputeGrassWeight;
			modelDesc.minAreaFrec = 0.005f;
			modelDesc.path = "assets/model/Stylized/StylizedGrass.gltf";
			bool existingModel = modelAssetMgr->Add(modelDesc, grassModelHandle);
			modelDesc.pCustomNrmWFunc = nullptr;

			// 新規の場合、草のマテリアルに草揺れ用CBVをセット
			if (!existingModel)
			{
				auto data = modelAssetMgr->GetWrite(grassModelHandle);
				auto& submesh = data.ref().subMeshes;

				for (auto& mesh : submesh)
				{
					auto matData = materialMgr->GetWrite(mesh.material);

					//頂点シェーダーにもバインドする設定にする
					matData.ref().isBindVSSampler = true;

					for (auto& tpx : mesh.lodThresholds.Tpx) // LOD調整
					{
						tpx *= 4.0f;
					}
				}
			}

			modelDesc.ClearAdditionalBindings();

			ModelAssetHandle ruinTowerModelHandle;
			modelDesc.instancesPeak = 2;
			modelDesc.viewMax = 1000.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.minAreaFrec = 0.0f;
			modelDesc.path = "assets/model/Ruins/RuinTower.gltf";
			modelDesc.buildOccluders = true;
			existingModel = modelAssetMgr->Add(modelDesc, ruinTowerModelHandle);

			if (!existingModel && modelDesc.buildOccluders)
			{
				auto ruinTowerData = modelAssetMgr->GetWrite(ruinTowerModelHandle);
				// 遮蔽AABBを少し小さくする
				auto& occAABB = ruinTowerData.ref().subMeshes[0].occluder.meltAABBs[0];
				occAABB.lb.x *= 0.4f;
				occAABB.lb.z *= 0.4f;
				occAABB.ub.x *= 0.4f;
				occAABB.ub.z *= 0.4f;
			}


			ModelAssetHandle ruinBreakTowerModelHandle;
			modelDesc.path = "assets/model/Ruins/RuinBreakTowerA.gltf";
			//中に入るタイプのモデルのオクル―ダーメッシュはまだできていないのでとりあえずfalse
			modelDesc.buildOccluders = false;
			existingModel = modelAssetMgr->Add(modelDesc, ruinBreakTowerModelHandle);


			ModelAssetHandle ruinStoneModelHandle;
			modelDesc.instancesPeak = 10;
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.path = "assets/model/Ruins/RuinStoneA.gltf";
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを
			modelDesc.buildOccluders = true;
			modelAssetMgr->Add(modelDesc, ruinStoneModelHandle);

			auto ps = serviceLocator->Get<Physics::PhysicsService>();

			std::function<Physics::ShapeHandle(Math::Vec3f)> makeShapeHandleFunc[5] =
			{
				[&](Math::Vec3f scale)
				{
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_1.chullbin", true, scale);
				},
				[&](Math::Vec3f scale)
				{
					Physics::ShapeCreateDesc shapeDesc;
					shapeDesc.shape = Physics::CapsuleDesc{ 8.0f,0.5f };
					shapeDesc.localOffset.y = 8.0f;
					return ps->MakeShape(shapeDesc);
				},
				nullptr,
				nullptr,
				nullptr
			};


			clock_t end = clock();

			const double time = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000.0;
			printf("create entity time %lf[ms]\n", time);

			std::random_device rd;
			std::mt19937_64 rng(rd());

			// 例: A=50%, B=30%, C=20% のつもりで重みを設定（整数でも実数でもOK）
			std::array<int, 5> weights{ 2, 8, 5, 5, 5 };
			std::discrete_distribution<int> dist(weights.begin(), weights.end());

			float modelScaleBase[5] = { 2.5f,4.0f,1.5f, 1.5f,1.5f };
			int modelScaleRange[5] = { 150,25,25, 25,25 };
			int modelRotRange[5] = { 360,360,360, 360,360 };
			bool enableOutline[5] = { true,true,false,false,false };

			std::vector<Math::Vec2f> grassAnchor;
			{
				auto data = modelAssetMgr->Get(grassModelHandle);
				auto aabb = data.ref().subMeshes[0].aabb;
				grassAnchor.reserve(4);
				float bias = 0.8f;
				grassAnchor.push_back({ aabb.lb.x * bias, aabb.lb.z * bias });
				grassAnchor.push_back({ aabb.lb.x * bias, aabb.ub.z * bias });
				grassAnchor.push_back({ aabb.ub.x * bias, aabb.lb.z * bias });
				grassAnchor.push_back({ aabb.ub.x * bias, aabb.ub.z * bias });
			}

			const auto& tp = params.terrainParams;
			const auto& terrain = params.terrainClustered;
			const auto& cpuSplatImage = params.cpuSplatImage;
			int terrainRank = params.terrainRank;

			//草Entity生成
			Math::Vec2f terrainScale = {
				tp.cellsX * tp.cellSize,
				tp.cellsZ * tp.cellSize
			};

			auto levelSession = pLevel->GetSession();

			for (int j = 0; j < (100 * terrainRank); ++j) {
				for (int k = 0; k < (100 * terrainRank); ++k) {
					for (int n = 0; n < 1; ++n) {
						float scaleXZ = 15.0f;
						float scaleY = 15.0f;
						Math::Vec2f offsetXZ = { 12.0f,12.0f };
						Math::Vec3f location = { float(j) * scaleXZ / 2.0f + offsetXZ.x , 0, float(k) * scaleXZ / 2.0f + offsetXZ.y };
						auto pose = terrain.SolvePlacementByAnchors(location, 0.0f, scaleXZ, grassAnchor);

						float height = 0.0f;
						terrain.SampleHeightNormalBilinear(location.x, location.z, height);
						location.y = height;

						int col = (int)(std::clamp((location.x / terrainScale.x), 0.0f, 1.0f) * cpuSplatImage.width);
						int row = (int)(std::clamp((location.z / terrainScale.y), 0.0f, 1.0f) * cpuSplatImage.height);

						int byteIndex = col * 4 + row * cpuSplatImage.stride;
						if (byteIndex < 0 || byteIndex >= (int)cpuSplatImage.bytes.size()) {
							continue;
						}

						auto splatR = cpuSplatImage.bytes[byteIndex];
						if (splatR < 15) {
							continue; // 草が薄い場所はスキップ
						}

						//　薄いほど高さを下げる
						float t = 1.0f - splatR / 255.0f; // 0..1
						constexpr float k = 5.0f;            // カーブの強さ（お好み）

						// 0..1 に正規化した exp カーブ
						float w = (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f); // w: 0..1

						location.y -= w * 2.0f;   // 最大で 4 下げる（0..4）

						auto rot = Math::QuatFromBasis(pose.right, pose.up, pose.forward);
						auto modelComp = CModel{ grassModelHandle };
						rot.KeepTwist(pose.up);
						auto id = levelSession.AddEntity(
							CTransform{ location, rot, Math::Vec3f(scaleXZ,scaleY,scaleXZ) },
							std::move(modelComp)
						);
					}
				}
			}

			// 点光源生成
			/*for (int j = 0; j < 10; ++j) {
				for (int k = 0; k < 10; ++k) {
					for (int n = 0; n < 1; ++n) {
						float scaleXZ = 15.0f;
						float scaleY = 15.0f;
						Math::Vec2f offsetXZ = { 12.0f,12.0f };
						Math::Vec3f location = { float(j) * scaleXZ + offsetXZ.x , 0, float(k) * scaleXZ + offsetXZ.y };

						float height = 0.0f;
						terrain.SampleHeightNormalBilinear(location.x, location.z, height);
						location.y = height + 5.0f;

						Graphics::PointLightDesc plDesc;
						plDesc.positionWS = location;
						plDesc.color = { 1.0f,0.8f,0.6f };
						plDesc.intensity = 0.5f;
						plDesc.range = 10.0f;
						plDesc.castsShadow = false;
						auto plHandle = pointLightService.Create(plDesc);

						levelSession.AddEntity(
							CPointLight{ plHandle }
						);
					}
				}
			}*/

			auto playerService = serviceLocator->Get<PlayerService>();
			Math::Vec3f playerStartPos = playerService->GetPlayerPosition();

			auto perCameraService = serviceLocator->Get<Graphics::I3DPerCameraService>();
			auto camRot = perCameraService->GetRotation();
			Math::Vec3f camR, camU, camF;
			Math::ToBasis<float, Math::LH_ZForward>(camRot, camR, camU, camF);
			Math::Vec2f camDirXZ = Math::Vec2f{ camF.x, camF.z }.normalized();

			auto camFocusDis = perCameraService->GetFocusDistance();
			auto camFOVHalf = perCameraService->GetFOV() / 2.0f;

			auto getTerrainLocation = [&](float u, float v) {

				Math::Vec3f location = { tp.cellsX * tp.cellSize * u, 0.0f, tp.cellsZ * tp.cellSize * v };
				terrain.SampleHeightNormalBilinear(location.x, location.z, location.y);
				return location;
				};

			// Entity生成
			std::uniform_int_distribution<uint32_t> distX(0, (std::numeric_limits< uint32_t>::max)());
			std::uniform_int_distribution<uint32_t> distZ(0, (std::numeric_limits< uint32_t>::max)());


			for (int j = 0; j < (100 * terrainRank); ++j) {
				for (int k = 0; k < (100 * terrainRank); ++k) {
					for (int n = 0; n < 1; ++n) {
						float u = distX(rng) / float((std::numeric_limits< uint32_t>::max)());
						float v = distZ(rng) / float((std::numeric_limits< uint32_t>::max)());
						Math::Vec3f location = getTerrainLocation(u, v);
						//Math::Vec3f location = { 30.0f + j * 10.0f,0.0f, 30.0f + k * 10.0f};

						int modelIdx = dist(rng);

						Math::Vec2f dirXZ = {
							 playerStartPos.x - location.x,
							 playerStartPos.z - location.z
						};

						// プレイヤー近くアウトライン有効モデルが選ばれたらやり直し
						const auto d2 = std::powf(START_CAMERA_PLAYER_DISTANCE + camFocusDis, 2.0f);
						if (dirXZ.lengthSquared() < d2) {

							dirXZ = dirXZ.normalized();
							float cosAngle = dirXZ.dot(camDirXZ);
							float angle = std::acosf(cosAngle);
							if (angle < camFOVHalf)
							{
								while (enableOutline[modelIdx])
								{
									modelIdx = dist(rng);
								}
							}
						}

						float scale = modelScaleBase[modelIdx] + float(rand() % modelScaleRange[modelIdx] - modelScaleRange[modelIdx] / 2) / 100.0f;
						//float scale = 1.0f;
						auto rot = Math::Quatf::FromAxisAngle({ 0,1,0 }, Math::Deg2Rad(float(rand() % modelRotRange[modelIdx])));
						auto modelComp = CModel{ modelAssetHandle[modelIdx] };
						modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
						modelComp.flags |= enableOutline[modelIdx] ? (uint16_t)EModelFlag::Outline : (uint16_t)EModelFlag::None;

						if (makeShapeHandleFunc[modelIdx] != nullptr)
						{
							auto chunk = pLevel->GetChunk(location);
							auto key = chunk.value()->GetNodeKey();
							CSpatialMotionTag tag{};
							tag.handle = { key, chunk.value() };

							Physics::CPhyBody staticBody{};
							staticBody.type = Physics::BodyType::Static; // staticにする
							staticBody.layer = Physics::Layers::NON_MOVING_RAY_IGNORE;
							auto shapeHandle = makeShapeHandleFunc[modelIdx](Math::Vec3f(scale, scale, scale));
#ifdef _ENABLE_IMGUI
							auto shapeDims = ps->GetShapeDims(shapeHandle);
#endif
							auto id = levelSession.AddEntity(
								CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
								modelComp,
								staticBody,
								//Physics::PhysicsInterpolation(
								//	location, // 初期位置
								//	rot // 初期回転
								//),
#ifdef _ENABLE_IMGUI
								shapeDims.value(),
#endif
								tag
							);
							if (id) {
								ps->EnqueueCreateIntent(id.value(), shapeHandle, key);
							}
						}
						else
						{
							levelSession.AddEntity(
								CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
								modelComp
							);
						}
					}
				}
			}

			//プレイヤー生成
			{
				Physics::ShapeCreateDesc shapeDesc;
				shapeDesc.shape = Physics::CapsuleDesc{ 2.0f, 1.0f };
				shapeDesc.localOffset.y += 2.0f;
				auto playerShape = ps->MakeShape(shapeDesc);
#ifdef _ENABLE_IMGUI
				auto playerDims = ps->GetShapeDims(playerShape);
#endif

				CModel modelComp{ playerModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				auto id = levelSession.AddGlobalEntity(
					CTransform{ playerStartPos ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } },
					modelComp,
					PlayerComponent{}
#ifdef _ENABLE_IMGUI
					, playerDims.value()
#endif
				);
				if (id) {
					Physics::CreateCharacterCmd c(id.value());
					c.shape = playerShape;
					c.worldTM.pos = playerStartPos;
					c.objectLayer = Physics::Layers::MOVING;

					ps->CreateCharacter(c);
					//ps->EnqueueCreateIntent(id.value(), playerShape, key);
				}
			}

			//地形コリジョン生成
			{
				Physics::ShapeCreateDesc terrainShapeDesc;
				terrainShapeDesc.shape = Physics::HeightFieldDesc{
					.sizeX = (int)tp.cellsX + 1,
					.sizeY = (int)tp.cellsZ + 1,
					.samples = params.heightMap,
					.scaleY = tp.heightScale,
					.cellSizeX = tp.cellSize,
					.cellSizeY = tp.cellSize
				};
				auto terrainShape = ps->MakeShape(terrainShapeDesc);
				Physics::CPhyBody terrainBody{};
				terrainBody.type = Physics::BodyType::Static; // staticにする
				terrainBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;
				auto id = levelSession.AddEntity(
					CTransform{ tp.offset.x, tp.offset.y, tp.offset.z ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
					terrainBody
				);
				if (id) {
					auto chunk = pLevel->GetChunk({ 0.0f, -40.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
					ps->EnqueueCreateIntent(id.value(), terrainShape, chunk.value()->GetNodeKey());
				}
			}

			// 塔生成
			{
				Math::Vec3f location = getTerrainLocation(0.7f, 0.7f);
				location.y -= 10.0f; // 少し埋める

				auto shape = ps->MakeMesh("generated/meshshape/Ruins/RuinTower.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
				auto shapeDims = ps->GetShapeDims(shape);
#endif
				CModel modelComp{ ruinTowerModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

				Physics::CPhyBody staticBody{};
				staticBody.type = Physics::BodyType::Static; // staticにする
				staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

				auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };

				auto id = levelSession.AddGlobalEntity(
					tf,
					modelComp,
					staticBody
#ifdef _ENABLE_IMGUI
					, shapeDims.value()
#endif
				);
				if (id) {
					// チャンクに属さないので直接ボディ作成コマンドを発行
					auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
					ps->CreateBody(bodyCmd);
				}
			}

			// 壊れた塔生成
			{
				Math::Vec3f location = getTerrainLocation(0.4f, 0.62f);
				location.y -= 4.0f; // 少し埋める

				auto shape = ps->MakeMesh("generated/meshshape/Ruins/RuinBreakTowerA.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
				auto shapeDims = ps->GetShapeDims(shape);
#endif
				CModel modelComp{ ruinBreakTowerModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

				Physics::CPhyBody staticBody{};
				staticBody.type = Physics::BodyType::Static; // staticにする
				staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

				auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };

				auto id = levelSession.AddGlobalEntity(
					tf,
					modelComp,
					staticBody
#ifdef _ENABLE_IMGUI
					, shapeDims.value()
#endif
				);
				if (id) {
					// チャンクに属さないので直接ボディ作成コマンドを発行
					auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
					ps->CreateBody(bodyCmd);
				}
			}

			//石碑生成
			{
				Math::Vec3f location = getTerrainLocation(0.3f, 0.3f);
				location.y -= 4.0f; // 少し埋める

				auto shape = ps->MakeConvexCompound("generated/convex/Ruins/RuinStoneA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
				auto shapeDims = ps->GetShapeDims(shape);
#endif
				CModel modelComp{ ruinStoneModelHandle };
				modelComp.flags = (uint16_t)EModelFlag::CastShadow;
				Physics::CPhyBody staticBody{};
				staticBody.type = Physics::BodyType::Static; // staticにする
				staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;
				auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };
				auto id = levelSession.AddGlobalEntity(
					tf,
					modelComp,
					staticBody
#ifdef _ENABLE_IMGUI
					, shapeDims.value()
#endif
				);
				if (id) {
					// チャンクに属さないので直接ボディ作成コマンドを発行
					auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
					ps->CreateBody(bodyCmd);
				}
			}

			//蛍の領域生成
			{
				Math::Vec3f location = getTerrainLocation(0.42f, 0.58f);
				//location.y += 5.0f; // 少し浮かせる

				CFireflyVolume fireflyVolume;
				fireflyVolume.centerWS = location;
				fireflyVolume.hitRadius = 40.0f;
				fireflyVolume.radius = 50.0f;

				//位置を指定して追加
				levelSession.AddEntityWithLocation(fireflyVolume.centerWS, fireflyVolume);
			}

			//葉っぱの領域生成
			{
				Math::Vec3f location = getTerrainLocation(0.42f, 0.54f);
				//location.y += 5.0f; // 少し浮かせる

				CLeafVolume leafVolume;
				leafVolume.centerWS = location;
				leafVolume.radius = 40.0f;
				leafVolume.farDistance = 60.0f;
				leafVolume.k = 20.0f;

				auto chunk = pLevel->GetChunk(location);
				auto key = chunk.value()->GetNodeKey();

				//動く前提でチャンク移動用のタグを付与
				CSpatialMotionTag tag{};
				tag.handle = { key, chunk.value() };

				//位置を指定して追加
				levelSession.AddEntityWithLocation(leafVolume.centerWS, leafVolume, tag);
			}


			// System登録
			auto& scheduler = pLevel->GetScheduler();

			scheduler.AddSystem<ModelRenderSystem>(*serviceLocator);

			//scheduler.AddSystem<SimpleModelRenderSystem>(*serviceLocator);
			//scheduler.AddSystem<PhysicsSystem>(*serviceLocator);
			scheduler.AddSystem<BuildBodiesFromIntentsSystem>(*serviceLocator);
			scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(*serviceLocator);
			//scheduler.AddSystem<PlayerSystem>(*serviceLocator);
			scheduler.AddSystem<PointLightSystem>(*serviceLocator);
			scheduler.AddSystem<FireflySystem>(*serviceLocator);
			scheduler.AddSystem<LeafSystem>(*serviceLocator);
			//scheduler.AddSystem<CleanModelSystem>(*serviceLocator);

#ifdef _ENABLE_IMGUI
			scheduler.AddSystem<DebugRenderSystem>(*serviceLocator);
#endif

			//カスタムの処理を開始
			ctx.executeCustom.store(true, std::memory_order_relaxed);

		},
		//アンロード時
		[&](const ECS::ServiceLocator*, OpenFieldLevel* pLevel)
		{
			ctx.executeCustom.store(false, std::memory_order_relaxed);
		});

	// レベル追加コマンドを実行キューにプッシュ
	worldRequestService.PushCommand(std::move(reqCmd));
}
