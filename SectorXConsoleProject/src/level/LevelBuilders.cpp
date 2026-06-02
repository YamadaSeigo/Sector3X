#include "LevelBuilders.h"
#include "app/AppContext.h"
#include "app/appconfig.h"
#include "environment/BiomeScatterGenerator.h"
#include "environment/FenceLine.h"

#include <SectorFW/Graphics/ImageLoader.h>

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
#include "system/ChasePlayerSystem.h"
#include "system/EnvironmentSystem.h"
#include "system/LightShadowSystem.h"
#include "system/PointLightSystem.h"
#include "system/SpriteAnimationSystem.h"
#include "system/FireflySystem.h"
#include "system/LeafSystem.h"
#include "system/TitleSystem.h"

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
			ShaderHandle windSpriteShaderHandle;
			shaderMgr->Add(shaderDesc, windSpriteShaderHandle);

			DX11::PSOCreateDesc psoDesc = { windSpriteShaderHandle, RasterizerStateID::SolidCullBack };
			PSOHandle psoHandle;
			psoMgr->Add(psoDesc, psoHandle);

			shaderDesc.vsPath = L"assets/shader/VS_ClipUVColor.cso";
			shaderDesc.psPath = L"assets/shader/PS_CircleAlpha.cso";
			ShaderHandle circleAplhaHandle;
			shaderMgr->Add(shaderDesc, circleAplhaHandle);

			psoDesc = { circleAplhaHandle, RasterizerStateID::SolidCullBack };
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

			matDesc.shader = windSpriteShaderHandle;
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

			textureDesc.path = "assets/texture/sprite/PressSpace.png";
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

			Math::Quatf yawRot = Math::Quatf::FromAxisAngle({ 0.0f,1.0f,0.0f }, Math::Deg2Rad(55.0f));

			auto pitchRot = Math::Quatf::FromAxisAngle({ 1.0f,0.0f,0.0f }, Math::Deg2Rad(-20.0f));
			auto camRot = yawRot * pitchRot;

			perCameraService->SetRotation(camRot);

			Math::Vec3f r, u, f;
			Math::ToBasis<float, Math::LH_ZForward>(yawRot, r, u, f);

			perCameraService->SetTarget(pp - f * START_CAMERA_PLAYER_DISTANCE + Math::Vec3f{ 0.0f, 8.0f, 0.0f });

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
			shaderDesc.psPath = L"assets/shader/PS_OpaqueColor.cso";
			shaderMgr->Add(shaderDesc, shaderHandle);
			PSOHandle windGrassPSOHandle;
			psoDesc.shader = shaderHandle;
			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
			psoMgr->Add(psoDesc, windGrassPSOHandle);
			psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

			shaderDesc.vsPath = L"assets/shader/VS_WindEntity.cso";
			shaderDesc.psPath = L"assets/shader/PS_OpaqueColor.cso";
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

			enum BiomeObjectType {
				RockA,
				RockB,
				RockC,
				TreeA,
				TreeB,
				TreeC,
				YellowFlower,
				WhiteCosmos,
				YellowCosmos,
				LightFlower,
				WoodFence,
				BiomeObjectCount
			};

			ModelAssetHandle modelAssetHandle[BiomeObjectCount];

			auto windCBHandle = ctx.wind->GetBufferHandle();
			auto footCBHandle = ctx.player->GetFootBufferHandle();

			auto materialMgr = graphics.GetRenderService()->GetResourceManager<DX11::MaterialManager>();
			// モデルアセットの読み込み
			DX11::ModelAssetCreateDesc modelDesc;
			modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_1.gltf";
			modelDesc.pso = cullDefaultPSOHandle;
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
			modelDesc.instancesPeak = 200;
			modelDesc.viewMax = 50.0f;
			modelDesc.buildOccluders = false;

			modelAssetMgr->Add(modelDesc, modelAssetHandle[RockA]);
			modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_2.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[RockB]);
			modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_3.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[RockC]);

			modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
			modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

			modelDesc.path = "assets/model/Stylized/Tree01.gltf";
			modelDesc.viewMax = 50.0f;
			modelDesc.buildOccluders = false;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.minAreaFrec = 0.001f;
			modelDesc.pCustomNrmWFunc = WindService::ComputeTreeWeight;
			modelAssetMgr->Add(modelDesc, modelAssetHandle[TreeA]);

			modelDesc.path = "assets/model/Stylized/Tree02.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[TreeB]);

			modelDesc.path = "assets/model/Stylized/Tree03.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[TreeC]);

			modelDesc.path = "assets/model/Stylized/YellowFlower.gltf";
			modelDesc.buildOccluders = false;
			modelDesc.viewMax = 50.0f;
			modelDesc.minAreaFrec = 0.0004f;
			modelDesc.pCustomNrmWFunc = WindService::ComputeGrassWeight;
			modelDesc.pso = cullNoneWindEntityPSOHandle;

			modelAssetMgr->Add(modelDesc, modelAssetHandle[YellowFlower]);

			modelDesc.instancesPeak = 100;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.pCustomNrmWFunc = WindService::ComputeGrassWeight;
			modelDesc.minAreaFrec = 0.0004f;
			modelDesc.path = "assets/model/Stylized/WhiteCosmos.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[WhiteCosmos]);

			modelDesc.instancesPeak = 100;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelDesc.path = "assets/model/Stylized/YellowCosmos.gltf";
			modelAssetMgr->Add(modelDesc, modelAssetHandle[YellowCosmos]);

			modelDesc.path = "assets/model/Stylized/BlueLightFlower.gltf";
			modelDesc.instancesPeak = 100;
			modelDesc.viewMax = 50.0f;
			modelDesc.pso = cullNoneWindEntityPSOHandle;
			modelAssetMgr->Add(modelDesc, modelAssetHandle[LightFlower]);

			modelDesc.path = "assets/model/Static/fence/fence01.gltf";
			modelDesc.instancesPeak = 20;
			modelDesc.viewMax = 80.0f;
			modelDesc.pso = cullDefaultPSOHandle;
			modelAssetMgr->Add(modelDesc, modelAssetHandle[WoodFence]);

			modelDesc.ClearAdditionalBindings();

			Math::AABB3f modelBounds[_countof(modelAssetHandle)];
			{
				auto readLock = modelAssetMgr->AcquireReadLock();
				for (int i = 0; i < _countof(modelAssetHandle); ++i)
				{
					const auto& modelData = modelAssetMgr->GetNoLock(modelAssetHandle[i]);
					for (const auto& mesh : modelData.subMeshes)
					{
						modelBounds[i].expandToInclude(mesh.aabb);
					}
				}
			}

			ModelAssetHandle playerModelHandle;
			modelDesc.pso = cullDefaultPSOHandle;
			modelDesc.path = "assets/model/BlackGhost.glb";
			modelDesc.pCustomNrmWFunc = nullptr;
			modelDesc.minAreaFrec = 0.001f;
			modelAssetMgr->Add(modelDesc, playerModelHandle);

			ModelAssetHandle playerLanternModelHandle;
			modelDesc.path = "assets/model/Light/fantasy_lantern.gltf";
			modelDesc.minAreaFrec = 0.0f;
			modelAssetMgr->Add(modelDesc, playerLanternModelHandle);

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
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.minAreaFrec = 0.0f;
			modelDesc.path = "assets/model/Ruins/Tower/RuinTower.gltf";
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

			ModelAssetHandle bridgeModelHandle;
			modelDesc.path = "assets/model/Static/Bridge/medieval_bridge.gltf";
			modelDesc.buildOccluders = false;
			modelAssetMgr->Add(modelDesc, bridgeModelHandle);

			ModelAssetHandle treeBridgeModelHandle;
			modelDesc.path = "assets/model/Static/Bridge/GiantTreeBridge.gltf";
			modelDesc.buildOccluders = false;
			modelAssetMgr->Add(modelDesc, treeBridgeModelHandle);

			ModelAssetHandle ruinBreakTowerModelHandle;
			modelDesc.path = "assets/model/Ruins/BreakTower/RuinBreakTowerA.gltf";
			//中に入るタイプのモデルのオクル―ダーメッシュはまだできていないのでとりあえずfalse
			modelDesc.buildOccluders = false;
			modelAssetMgr->Add(modelDesc, ruinBreakTowerModelHandle);

			ModelAssetHandle ruinStoneModelHandle;
			modelDesc.instancesPeak = 10;
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.path = "assets/model/Ruins/StoneA/RuinStoneA.gltf";
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを
			modelDesc.buildOccluders = true;
			modelAssetMgr->Add(modelDesc, ruinStoneModelHandle);

			std::string housePath[] = {
				"assets/model/Static/House/HouseA.gltf",
				"assets/model/Static/House/HouseB.gltf",
				"assets/model/Static/House/HouseC.gltf",
				"assets/model/Static/House/HouseD.gltf",
			};

			ModelAssetHandle houseModelHandle[_countof(housePath)];
			modelDesc.instancesPeak = 2;
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.buildOccluders = false;
			for (int i = 0; i < _countof(housePath); ++i)
			{
				modelDesc.path = housePath[i];
				modelAssetMgr->Add(modelDesc, houseModelHandle[i]);
			}

			std::string clusterRockPath[3] = {
				"assets/model/Static/ClusterRock/ClusterRockA.gltf",
				"assets/model/Static/ClusterRock/ClusterRockB.gltf",
				"assets/model/Static/ClusterRock/ClusterRockC.gltf",
			};

			ModelAssetHandle clusterRockModelHandle[3];

			modelDesc.instancesPeak = 5;
			modelDesc.buildOccluders = true;
			for (int i = 0; i < 3; ++i)
			{
				modelDesc.path = clusterRockPath[i];
				modelAssetMgr->Add(modelDesc, clusterRockModelHandle[i]);
			}

			std::string landmarkCrystalPath[3] = {
				"assets/model/landmark/crystals/hugeCrystal.gltf",
				"assets/model/landmark/crystals/largeCrystal.gltf",
				"assets/model/landmark/crystals/bigCrystal.gltf",
			};

			ModelAssetHandle landmarkCrystalModelHandle[3];
			modelDesc.instancesPeak = 10;
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを
			modelDesc.buildOccluders = false;
			for (int i = 0; i < 3; ++i)
			{
				modelDesc.path = landmarkCrystalPath[i];
				modelAssetMgr->Add(modelDesc, landmarkCrystalModelHandle[i]);
			}

			auto ps = serviceLocator->Get<Physics::PhysicsService>();
			auto pointLightService = serviceLocator->Get<Graphics::PointLightService>();

			std::function<Physics::ShapeHandle(Math::Vec3f)> makeShapeHandleFunc[BiomeObjectCount] = { nullptr };

			makeShapeHandleFunc[RockA] = [&](Math::Vec3f scale)
				{
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_1.chullbin", true, scale);
				};
			makeShapeHandleFunc[RockB] = [&](Math::Vec3f scale)
				{
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_2.chullbin", true, scale);
				};
			makeShapeHandleFunc[RockC] = [&](Math::Vec3f scale)
				{
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_3.chullbin", true, scale);
				};

			makeShapeHandleFunc[TreeA] = makeShapeHandleFunc[TreeB] = makeShapeHandleFunc[TreeC] = [&](Math::Vec3f scale)
				{
					Physics::ShapeCreateDesc shapeDesc;
					shapeDesc.shape = Physics::CapsuleDesc{ 10.0f,0.5f };
					shapeDesc.localOffset.y = 10.0f;
					return ps->MakeShape(shapeDesc);
				};

			makeShapeHandleFunc[WoodFence] = [&](Math::Vec3f scale)
				{
					return ps->MakeConvexCompound("generated/convex/Static/fence/fence01.chullbin", true, scale);
				};

			Graphics::PointLightDesc(*pMakePointLightDescFunc[BiomeObjectCount])(Math::Vec3f) = { nullptr };
			pMakePointLightDescFunc[LightFlower] = [](Math::Vec3f location) {
				Graphics::PointLightDesc plDesc;
				plDesc.offsetWS = Math::Vec3f(0.0f, 3.0f, 0.0f); // 少し上にずらす
				plDesc.color = { 0.2f,0.2f,1.0f }; // 青色
				plDesc.intensity = 1.0f;
				plDesc.range = 10.0f;
				plDesc.castsShadow = false;
				return plDesc;
				};

			bool enableOutline[BiomeObjectCount] = { false };
			enableOutline[RockA] = enableOutline[RockB] = enableOutline[RockC] = true;
			enableOutline[TreeA] = enableOutline[TreeB] = enableOutline[TreeC] = true;

			clock_t end = clock();

			const double time = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000.0;
			printf("create entity time %lf[ms]\n", time);

			Math::AABB3f grassBounds;
			std::vector<Math::Vec2f> grassAnchor;
			{
				auto data = modelAssetMgr->Get(grassModelHandle);
				for (auto& mesh : data.ref().subMeshes)
				{
					grassBounds.expandToInclude(mesh.aabb);
				}
				grassAnchor.reserve(4);
				float bias = 0.8f;
				grassAnchor.push_back({ grassBounds.lb.x * bias, grassBounds.lb.z * bias });
				grassAnchor.push_back({ grassBounds.lb.x * bias, grassBounds.ub.z * bias });
				grassAnchor.push_back({ grassBounds.ub.x * bias, grassBounds.lb.z * bias });
				grassAnchor.push_back({ grassBounds.ub.x * bias, grassBounds.ub.z * bias });
			}

			const auto& tp = params.terrainParams;
			const auto& terrain = params.terrainClustered;
			const auto& cpuSplatImage = params.cpuSplatImage;
			int terrainRank = params.terrainRank;

			auto biomeImg = Graphics::LoadImageFromFileRGBA8(
				"assets/texture/biome/biomeDSFT.png"
			);

			struct ImageRGBA {
				uint8_t r, g, b, a;
			};

			int stride = biomeImg.width * biomeImg.channels;

			auto sampBiome = [&](float u, float v) -> ImageRGBA {
				int x = (int)std::floor(biomeImg.width * u);
				int y = (int)std::floor(biomeImg.height * v);

				ImageRGBA out;
				memcpy(&out, &biomeImg.pixels.get()[x * biomeImg.channels + y * stride], biomeImg.channels);

				return out;
				};

			//草Entity生成
			Math::Vec2f terrainScale = {
				tp.cellsX * tp.cellSize,
				tp.cellsZ * tp.cellSize
			};

			constexpr Math::Vec3f tintDesert = Math::Vec3f(2.10f, 0.8f, 0.8f);
			constexpr Math::Vec3f tintSwamp = Math::Vec3f(0.85f, 0.5f, 2.0f);
			constexpr Math::Vec3f tintForest = Math::Vec3f(0.95f, 1.05f, 0.95f);
			constexpr Math::Vec3f tintTundra = Math::Vec3f(0.95f, 0.98f, 1.05f);

			auto levelSession = pLevel->GetSession();

			for (int j = 0; j < (100 * terrainRank); ++j) {
				for (int k = 0; k < (100 * terrainRank); ++k) {
					for (int n = 0; n < 1; ++n) {
						constexpr Math::Vec2f posJitter = Math::Vec2f(0.2f, 0.2f);

						uint32_t hash = BiomeScatterGenerator::Hash2D((uint32_t)j, (uint32_t)k, 1234);

						float scaleXZ = 15.0f;
						float scaleY = 15.0f;
						Math::Vec2f offsetXZ = { 12.0f,12.0f };
						Math::Vec3f location = { float(j) * scaleXZ / 2.0f + offsetXZ.x , 0, float(k) * scaleXZ / 2.0f + offsetXZ.y };
						location.x += BiomeScatterGenerator::URange(hash, -posJitter.x, posJitter.x);
						location.z += BiomeScatterGenerator::URange(hash, -posJitter.y, posJitter.y);

						auto pose = terrain.SolvePlacementByAnchors(location, 0.0f, scaleXZ, grassAnchor);

						float height = 0.0f;
						terrain.SampleHeightNormalBilinear(location.x, location.z, height);
						location.y = height;

						float u = std::clamp((location.x / terrainScale.x), 0.0f, 1.0f);
						float v = std::clamp((location.z / terrainScale.y), 0.0f, 1.0f);

						int col = (int)(u * cpuSplatImage.width);
						int row = (int)(v * cpuSplatImage.height);

						int byteIndex = col * 4 + row * cpuSplatImage.stride;
						if (byteIndex < 0 || byteIndex >= (int)cpuSplatImage.bytes.size()) {
							continue;
						}

						auto splatR = cpuSplatImage.bytes[byteIndex];
						if (splatR < 15) {
							continue; // 草が薄い場所はスキップ
						}

						auto biome = sampBiome(u, v);

						Math::Vec3f tint = {
							tintDesert * (biome.r / 255.0f) +
							tintSwamp * (biome.g / 255.0f) +
							tintForest * (biome.b / 255.0f) +
							tintTundra * (biome.a / 255.0f)
						};

						constexpr Math::Vec3f tinJitter = Math::Vec3f(0.1f, 0.1f, 0.1f);

						tint.x += BiomeScatterGenerator::URange(hash, -tinJitter.x, tinJitter.x);
						tint.y += BiomeScatterGenerator::URange(hash, -tinJitter.y, tinJitter.y);
						tint.z += BiomeScatterGenerator::URange(hash, -tinJitter.z, tinJitter.z);

						//　薄いほど高さを下げる
						float t = 1.0f - splatR / 255.0f; // 0..1
						constexpr float k = 5.0f;            // カーブの強さ（お好み）

						// 0..1 に正規化した exp カーブ
						float w = (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f); // w: 0..1

						location.y -= w * 2.0f;   // 最大で 2 下げる（0..2）
						//scaleY *= (1.0f - w * 0.8f); // 最大で80%縮小

						Math::AABB3f boundsWS = grassBounds;
						boundsWS *= Math::Vec3f(scaleXZ, scaleY, scaleXZ);
						boundsWS += location;

						auto rot = Math::QuatFromBasis(pose.right, pose.up, pose.forward);
						rot.KeepTwist(pose.up);
						auto id = levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ location, rot, Math::Vec3f(scaleXZ,scaleY,scaleXZ) },
							CModel{ grassModelHandle },
							CColor{ {tint,1.0f} }
						);
					}
				}
			}

			// バイオーム散布用データ準備
			//=====================================================================================

			enum EBiomeType : uint16_t {
				Biome_Forest = 0,
				Biome_Grassland = 1,
			};

			BiomeScatterGenerator biomeGen;

			ScatterInputs scatterInputs;
			scatterInputs.worldSize = {
				tp.cellsX * tp.cellSize,
				tp.cellsZ * tp.cellSize
			};

			// 地表のテクスチャ（スプラットマップ）
			auto& groundRgba = scatterInputs.groundRgba;
			groundRgba.data = (Rgba8*)cpuSplatImage.bytes.data();
			groundRgba.w = cpuSplatImage.width;
			groundRgba.h = cpuSplatImage.height;
			groundRgba.stride = cpuSplatImage.stride / sizeof(Rgba8);

			// 湿り気マップ
			auto wetnessImg = Graphics::LoadImageFromFile("assets/texture/biome/wetness.png", 1, false);
			auto& wetnessRgba = scatterInputs.wetness;
			wetnessRgba.data = (uint8_t*)wetnessImg.pixels.get();
			wetnessRgba.w = wetnessImg.width;
			wetnessRgba.h = wetnessImg.height;
			wetnessRgba.stride = wetnessImg.width * wetnessImg.desiredChannels;

			// 草の生えにくい場所マップ
			auto noVegetationImg = Graphics::LoadImageFromFile("assets/texture/biome/noVegetation.png", 1, false);
			auto& noVegetationRgba = scatterInputs.noVegetation;
			noVegetationRgba.data = (uint8_t*)noVegetationImg.pixels.get();
			noVegetationRgba.w = noVegetationImg.width;
			noVegetationRgba.h = noVegetationImg.height;
			noVegetationRgba.stride = noVegetationImg.width * noVegetationImg.desiredChannels;

			// バイオーム定義(BiomeType参照)
			uint16_t biomeData[12][12] = {
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
			};

			scatterInputs.biomeId.data = &biomeData[0][0];
			scatterInputs.biomeId.w = 12;
			scatterInputs.biomeId.h = 12;
			scatterInputs.biomeId.stride = scatterInputs.biomeId.w;

			// 地形サンプリング関数
			auto pSamplerHeight = [](float x, float z, void* user) -> float {
				auto terrain = static_cast<const Graphics::TerrainClustered*>(user);
				float height = 0.0f;
				terrain->SampleHeightNormalBilinear(x, z, height);
				return height;
				};
			scatterInputs.sampleHeight = pSamplerHeight;

			// 傾斜角サンプリング関数
			auto pSamplerSlopeDeg = [](float x, float z, void* user) -> float {
				auto terrain = static_cast<const Graphics::TerrainClustered*>(user);
				float height = 0.0f;
				Math::Vec3f nrm;
				terrain->SampleHeightNormalBilinear(x, z, height, &nrm);
				constexpr Math::Vec3f up = { 0.0f,1.0f,0.0f };
				return Math::Rad2Deg(std::acos(Math::Dot(nrm, up)));
				};
			scatterInputs.sampleSlopeDeg = pSamplerSlopeDeg;

			scatterInputs.heightUser = (void*)&terrain;

			scatterInputs.globalSeed = 20050105;

			biomeGen.SetInputs(scatterInputs);

			ScatterConfig scatterConfig;
			scatterConfig.candidateCellSize = 4.0f;
			scatterConfig.noVegRejectThreshold = 0.5f;
			scatterConfig.maxInstances = ECS::MAX_ENTITY_NUM;
			biomeGen.SetConfig(scatterConfig);

			// Forest Biome
			BiomeParams forest;
			forest.biomeId = Biome_Forest;
			forest.baseDensityPerSquareMeter = 0.04f;
			{
				BranchGroup trees;
				trees.spawnProbability = 0.25f;
				trees.maxSlopeDeg = 25.0f;
				trees.scaleMin = 3.3f; trees.scaleMax = 4.0f;
				trees.wGrass = 1.2f; trees.wSnow = 0.6f;
				trees.models = { {TreeA, 1.0f} }; //モデルのバリエーションを増やす場合はここに書く

				BranchGroup rocks;
				rocks.spawnProbability = 0.05f;
				rocks.maxSlopeDeg = 60.0f;
				rocks.scaleMin = 2.0f; rocks.scaleMax = 3.5f;
				rocks.wRock = 1.5f; rocks.wGrass = 0.2f;
				rocks.models = { {RockA, 1.0f},{RockB, 1.0f},{RockC, 1.0f} };

				BranchGroup flowers;
				flowers.spawnProbability = 0.1f;
				flowers.maxSlopeDeg = 30.0f;
				flowers.scaleMin = 1.0f; flowers.scaleMax = 1.5f;
				flowers.wGrass = 1.0f; flowers.wSnow = 0.6f;
				flowers.models = { {YellowFlower, 0.5f}, {WhiteCosmos, 0.3f}, {YellowCosmos, 0.2f} };

				forest.branches = { trees, rocks, flowers };
			}

			// Grassland Biome
			BiomeParams grassLand;
			grassLand.biomeId = Biome_Grassland;
			grassLand.baseDensityPerSquareMeter = 0.04f;
			{
				BranchGroup trees;
				trees.spawnProbability = 0.02f;
				trees.maxSlopeDeg = 25.0f;
				trees.scaleMin = 3.3f; trees.scaleMax = 4.0f;
				trees.wGrass = 1.2f; trees.wSnow = 0.6f;
				trees.models = { {TreeA, 1.0f}, {TreeB, 1.0f},{TreeC, 1.0f} }; //モデルのバリエーションを増やす場合はここに書く

				BranchGroup rocks;
				rocks.spawnProbability = 0.05f;
				rocks.maxSlopeDeg = 60.0f;
				rocks.scaleMin = 2.0f; rocks.scaleMax = 3.5f;
				rocks.wRock = 1.5f; rocks.wGrass = 0.2f;
				rocks.models = { {RockA, 1.0f},{RockB, 1.0f},{RockC, 1.0f} };

				BranchGroup flowers;
				flowers.spawnProbability = 0.3f;
				flowers.maxSlopeDeg = 30.0f;
				flowers.scaleMin = 1.0f; flowers.scaleMax = 1.5f;
				flowers.wGrass = 1.0f; flowers.wSnow = 0.6f;
				flowers.models = { {YellowFlower, 0.5f}, {WhiteCosmos, 0.3f}, {YellowCosmos, 0.2f} };

				grassLand.branches = { trees, rocks, flowers };
			}

			std::vector<BiomeParams> biomes = { forest, grassLand };
			biomeGen.SetBiomes(biomes);

			auto scatterInstance = biomeGen.GenerateAll();

			// バイオーム散布Entity生成
			for (auto& inst : scatterInstance)
			{
				uint32_t modelIdx = inst.model;
				CModel modelComp = CModel{ modelAssetHandle[modelIdx] };

				modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				modelComp.flags |= enableOutline[modelIdx] ? (uint16_t)EModelFlag::Outline : (uint16_t)EModelFlag::None;

				float scale = inst.uniformScale;
				Math::Vec3f location = inst.offsetWS;
				Math::Quatf rot = Math::Quatf::FromAxisAngle({ 0,1,0 }, inst.yaw);

				float u = std::clamp((location.x / terrainScale.x), 0.0f, 1.0f);
				float v = std::clamp((location.z / terrainScale.y), 0.0f, 1.0f);

				auto biome = sampBiome(u, v);

				Math::Vec3f biomeTint = {
					tintDesert * (biome.r / 255.0f) +
					tintSwamp * (biome.g / 255.0f) +
					tintForest * (biome.b / 255.0f) +
					tintTundra * (biome.a / 255.0f)
				};

				Math::Vec3f tint = inst.tint * biomeTint;

				SFW::SpatialChunk::Bounds3f boundsWS = modelBounds[modelIdx];
				boundsWS *= scale; // スケール適用
				boundsWS += location; // ワールド位置に移動

				if (makeShapeHandleFunc[modelIdx] != nullptr)
				{
					auto chunk = pLevel->GetChunk(location);
					auto key = chunk.value()->GetNodeKey();

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_IGNORE;

					auto shapeHandle = makeShapeHandleFunc[modelIdx](Math::Vec3f(scale, scale, scale));
#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shapeHandle);
#endif

					std::optional<ECS::EntityID> id;
					if (pMakePointLightDescFunc[modelIdx] != nullptr)
					{
						auto plDesc = pMakePointLightDescFunc[modelIdx](location);
						auto plHandle = pointLightService->Create(plDesc);

						// 静的境界エンティティとして登録
						id = levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ inst.offsetWS, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {tint, 1.0f} },
							staticBody,
							CPointLight{ plHandle }
#ifdef _ENABLE_IMGUI
							, shapeDims.value()
#endif
						);
					}
					else {
						// 静的境界エンティティとして登録
						id = levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ inst.offsetWS, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {tint, 1.0f} },
							staticBody
#ifdef _ENABLE_IMGUI
							, shapeDims.value()
#endif
						);
					}
					if (id) {
						ps->EnqueueCreateIntent(id.value(), shapeHandle, key);
					}
				}
				else
				{
					if (pMakePointLightDescFunc[modelIdx] != nullptr)
					{
						auto plDesc = pMakePointLightDescFunc[modelIdx](location);
						auto plHandle = pointLightService->Create(plDesc);

						levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {tint, 1.0f} },
							CPointLight{ plHandle }
						);
					}
					else {
						levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {tint, 1.0f} }
						);
					}
				}
			}

			auto playerService = serviceLocator->Get<PlayerService>();
			Math::Vec3f playerStartPos = playerService->GetPlayerPosition();
			auto getTerrainLocation = [&](float u, float v) {
				Math::Vec3f location = { tp.cellsX * tp.cellSize * u, 0.0f, tp.cellsZ * tp.cellSize * v };
				terrain.SampleHeightNormalBilinear(location.x, location.z, location.y);
				return location;
				};

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
					CColor{ {1.0f,1.0f,1.0f,1.0f} },
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

			//プレイヤーに追従するライト生成
			for (int i = 0; i < 5; ++i)
			{
				Physics::ShapeCreateDesc shapeDesc;
				shapeDesc.shape = Physics::SphereDesc{ 0.5f };
				auto lanternShape = ps->MakeShape(shapeDesc);
#ifdef _ENABLE_IMGUI
				auto lanternDims = ps->GetShapeDims(lanternShape);
#endif

				Graphics::PointLightDesc plDesc;
				plDesc.range = 15.0f;
				plDesc.intensity = 1.2f;
				auto plHandle = pointLightService->Create(plDesc);

				Physics::CPhyBody phyBody{};
				phyBody.type = Physics::BodyType::Dynamic;
				phyBody.layer = Physics::Layers::MOVING;

				Math::Vec3f location = playerStartPos + Math::Vec3f{ 0.0f, 5.0f, 0.0f };

				auto chunk = pLevel->GetChunk(location);
				auto key = chunk.value()->GetNodeKey();

				//動く前提でチャンク移動用のタグを付与
				CSpatialMotionTag motionTag{};
				motionTag.handle = { key, chunk.value() };

				CModel modelComp{ playerLanternModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				auto id = levelSession.AddEntity(
					CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } },
					modelComp,
					phyBody,
					Physics::PhysicsInterpolation{ location, {0.0f,0.0f,0.0f,1.0f} },
					motionTag,
					CColor{ {1.0f,1.0f,1.0f,1.0f} },
					CPointLight{ plHandle },
#ifdef _ENABLE_IMGUI
					lanternDims.value(),
#endif
					CChasePlayer{}
				);
				if (id) {
					Physics::PhysicsService::Material phyMat;
					phyMat.gravityFactor = 0.0f; // 重力の影響を受けないようにする
					ps->EnqueueCreateIntent(id.value(), lanternShape, key, phyMat);
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

			// フェンス生成
			{
				std::vector<Math::Vec3f> road = {
					Math::Vec3f{607.215637f, 77.519569f, 599.671753f},
					Math::Vec3f{668.081726f, 68.777458f, 592.452515f},
					Math::Vec3f{731.580627f, 56.214619f, 591.262268f},
					Math::Vec3f{799.360229f, 48.126629f, 594.139160f},
					Math::Vec3f{861.856628f, 42.902073f, 597.171021f},
					Math::Vec3f{940.440857f, 36.755699f, 597.259888f},
					Math::Vec3f{1003.742432f, 39.001648f, 601.490173f},
					Math::Vec3f{1044.597412f, 47.029774f, 600.783386f},
					Math::Vec3f{1106.469727f, 50.481812f, 604.738098f},
					Math::Vec3f{1160.296509f, 49.414864f, 608.373108f},
					Math::Vec3f{1221.848877f, 42.603046f, 619.834839f},
					Math::Vec3f{1283.123779f, 48.596622f, 630.732971f},
					Math::Vec3f{1343.074341f, 49.404034f, 634.030273f},
					Math::Vec3f{1412.888062f, 52.946095f, 643.868225f},
					Math::Vec3f{1479.115356f, 49.775639f, 655.092285f},
					Math::Vec3f{1549.663330f, 34.118298f, 668.385803f},
					Math::Vec3f{1646.870850f, 33.255447f, 677.984009f},
					Math::Vec3f{1764.958496f, 47.995502f, 713.339539f},
					Math::Vec3f{1871.717651f, 43.683792f, 761.720520f},
					Math::Vec3f{1971.495361f, 32.922108f, 816.616272f},
					Math::Vec3f{2030.542236f, 32.146194f, 872.076111f},
					Math::Vec3f{2068.801514f, 30.124413f, 924.875671f},
					Math::Vec3f{2100.061768f, 36.532452f, 1004.549194f},
					Math::Vec3f{2129.464355f, 40.790668f, 1075.113281f},
				};

				FenceParams fp;
				fp.spacing = 200.0f;
				fp.spacingJitter = 100.0f;
				fp.baseWidth = 12.0f;
				fp.widthRand = 1.0f;
				fp.baseHeight = 0.0f;
				fp.heightRand = 0.15f;
				fp.yawJitterDeg = 3.0f;
				fp.bothSides = true;
				fp.randomSide = true; // 常に右側に置く

				auto fences = GenerateFenceAlongPolyline(road, fp, /*seed=*/12345);

				for (auto& fence : fences)
				{
					auto shape = ps->MakeConvexCompound("generated/convex/Static/fence/fence01.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });

#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shape);
#endif

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_IGNORE;

					// フェンスの位置は地面に合わせる
					Math::Vec3f outNrm = { 0.0f,1.0f,0.0f };
					bool sampleOK = terrain.SampleHeightNormalBilinear(fence.position.x, fence.position.z, fence.position.y, &outNrm);
					if (!sampleOK) continue;

					Math::Vec3f planeTangent = fence.tangent - outNrm * Math::Dot(fence.tangent, outNrm); // 接線ベクトルを地面に平行になるように修正
					Math::Vec3f right = Math::Normalize(Math::Cross(outNrm, planeTangent));
					Math::Quatf rot = Math::QuatFromBasis(right, outNrm, planeTangent);

					CModel modelComp{ modelAssetHandle[WoodFence] };
					modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

					Math::AABB3f boundsWS = modelBounds[WoodFence];
					boundsWS += fence.position; // ワールド位置に移動

					auto id = levelSession.AddStaticBoundsEntity(
						boundsWS,
						CTransform{ fence.position, rot, Math::Vec3f(1.0f, 1.0f,1.0f) },
						modelComp,
						CColor{ {1.0f,1.0f,1.0f,1.0f} },
						staticBody
#ifdef _ENABLE_IMGUI
						, shapeDims.value()
#endif
					);
					if (id) {
						auto chunk = pLevel->GetChunk(fence.position);
						ps->EnqueueCreateIntent(id.value(), shape, chunk.value()->GetNodeKey());
					}
				}
			}

			auto addGlobalEntityWithBody = [&](const Math::Vec2f posUV, float offsetY, CModel model, Physics::ShapeHandle shapeHandle, Math::Quatf rot = { 0.0f,0.0f,0.0f,1.0f }, Math::Vec3f scale = { 1.0f,1.0f,1.0f })
				{
					auto location = getTerrainLocation(posUV.x, posUV.y);
					location.y += offsetY;

#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shapeHandle);
#endif

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

					auto tf = CTransform{ location , rot , scale };

					auto id = levelSession.AddGlobalEntity(
						tf,
						model,
						CColor{ {1.0f,1.0f,1.0f,1.0f} },
						staticBody
#ifdef _ENABLE_IMGUI
						, shapeDims.value()
#endif
					);
					if (id) {
						// チャンクに属さないので直接ボディ作成コマンドを発行
						auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shapeHandle);
						ps->CreateBody(bodyCmd);
					}

					return id;
				};

			//橋生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Static/Bridge/medieval_bridge.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ bridgeModelHandle }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.647f, 0.27f }, 35.0f, modelComp, shape,
					Math::Quatf::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, Math::Deg2Rad(140.0f)));
			}

			//木の橋生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Static/Bridge/GiantTreeBridge.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ treeBridgeModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow | (uint16_t)EModelFlag::RainOccluder;
				addGlobalEntityWithBody({ 0.4f, 0.18f }, -20.0f, modelComp, shape,
					Math::Quatf::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, Math::Deg2Rad(90.0f)));
			}

			// 塔生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Ruins/Tower/RuinTower.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ ruinTowerModelHandle }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.67f, 0.51f }, -10.0f, modelComp, shape);
			}

			// 壊れた塔生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Ruins/BreakTower/RuinBreakTowerA.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ ruinBreakTowerModelHandle };
				modelComp.flags |= (uint16_t)EModelFlag::CastShadow | (uint16_t)EModelFlag::RainOccluder;
				addGlobalEntityWithBody({ 0.4f, 0.62f }, -4.0f, modelComp, shape);
			}

			//石碑生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/Ruins/StoneA/RuinStoneA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ ruinStoneModelHandle }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.35f, 0.26f }, -4.0f, modelComp, shape);
			}

			//家生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Static/House/HouseA.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ houseModelHandle[0] }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.55f, 0.45f }, -1.0f, modelComp, shape, Math::Quatf::FromAxisAngle({ 0.0f,1.0f,0.0f }, Math::Deg2Rad(60.0f)));

				shape = ps->MakeMesh("generated/meshshape/Static/House/HouseB.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp2{ houseModelHandle[1] }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.563f, 0.474f }, -1.0f, modelComp2, shape, Math::Quatf::FromAxisAngle({ 0.0f,1.0f,0.0f }, Math::Deg2Rad(-135.0f)));

				shape = ps->MakeMesh("generated/meshshape/Static/House/HouseC.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp3{ houseModelHandle[2] }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.531f, 0.47f }, -1.0f, modelComp3, shape, Math::Quatf::FromAxisAngle({ 0.0f,1.0f,0.0f }, Math::Deg2Rad(110.0f)));

				shape = ps->MakeMesh("generated/meshshape/Static/House/HouseD.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp4{ houseModelHandle[3] }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.531f, 0.438f }, -1.0f, modelComp4, shape, Math::Quatf::FromAxisAngle({ 0.0f,1.0f,0.0f }, Math::Deg2Rad(85.0f)));
			}

			//岩クラスター生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/Static/ClusterRock/ClusterRockA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp1{ clusterRockModelHandle[0] }; modelComp1.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.28f, 0.24f }, 5.0f, modelComp1, shape, Math::Quatf::FromEuler(0.0f, Math::Deg2Rad(90.0f), 0.0f));

				shape = ps->MakeConvexCompound("generated/convex/Static/ClusterRock/ClusterRockB.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp2{ clusterRockModelHandle[1] }; modelComp2.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.5f, 0.2f }, 5.0f, modelComp2, shape, Math::Quatf::FromEuler(0.0f, Math::Deg2Rad(40.0f), 0.0f));
			}

			//ランドマーククリスタル生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/landmark/crystals/hugeCrystal.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				CModel modelComp{ landmarkCrystalModelHandle[0] }; modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
				addGlobalEntityWithBody({ 0.8f, 0.8f }, -10.0f, modelComp, shape);

				auto pos = getTerrainLocation(0.8f, 0.8f);

				Graphics::PointLightDesc plDesc;
				plDesc.offsetWS = Math::Vec3f{ 0.0f,100.0f,0.0 };
				plDesc.intensity = 10.0f;
				plDesc.range = 300.0f;
				plDesc.color = { 1.0f, 0.2f, 1.0f };
				auto plHandle = pointLightService->Create(plDesc);
				levelSession.AddGlobalEntity(
					CTransform{ pos , {0.0f,0.0f,0.0f,1.0f}, Math::Vec3f(1.0f,1.0f,1.0f) },
					CPointLight{ plHandle }
				);
			}

			//蛍の領域生成
			{
				Math::Vec3f location = getTerrainLocation(0.3f, 0.215f);
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
				Math::Vec3f location = getTerrainLocation(0.26f, 0.2f);
				//location.y += 5.0f; // 少し浮かせる

				CLeafVolume leafVolume;
				leafVolume.centerWS = location;
				leafVolume.radius = 80.0f;
				leafVolume.farDistance = 100.0f;
				leafVolume.k = 30.0f;

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
			scheduler.AddSystem<PhysicsSystem>(*serviceLocator);
			scheduler.AddSystem<BuildBodiesFromIntentsSystem>(*serviceLocator);
			scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(*serviceLocator);
			scheduler.AddSystem<PlayerSystem>(*serviceLocator);
			scheduler.AddSystem<PointLightSystem>(*serviceLocator);
			scheduler.AddSystem<FireflySystem>(*serviceLocator);
			scheduler.AddSystem<LeafSystem>(*serviceLocator);
			scheduler.AddSystem<ChasePlayerSystem>(*serviceLocator);
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