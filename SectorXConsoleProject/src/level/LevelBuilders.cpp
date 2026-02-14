#include "LevelBuilders.h"
#include "app/AppContext.h"
#include "app/appconfig.h"
#include "environment/BiomeScatterGenerator.h"

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

			Math::Quatf rot = Math::Quatf::FromAxisAngle(u, Math::Deg2Rad(55.0f));
			perCameraService->Rotate(rot);

			camRot = perCameraService->GetRotation();
			Math::ToBasis<float, Math::LH_ZForward>(camRot, r, u, f);

			rot = Math::Quatf::FromAxisAngle(r, Math::Deg2Rad(-20.0f));
			perCameraService->Rotate(rot);

			camRot = perCameraService->GetRotation();
			Math::ToBasis<float, Math::LH_ZForward>(camRot, r, u, f);

			Math::Vec3f backward = Math::Cross({ 0.0f, 1.0f, 0.0f }, r);
			perCameraService->SetTarget(pp + backward * START_CAMERA_PLAYER_DISTANCE + Math::Vec3f{ 0.0f, 8.0f, 0.0f });


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

			enum BiomeObjectType {
				RockA,
				RockB,
				RockC,
				TreeA,
				YellowFlower,
				WhiteCosmos,
				YellowCosmos,
				LightFlower,
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


			ModelAssetHandle ruinBreakTowerModelHandle;
			modelDesc.path = "assets/model/Ruins/BreakTower/RuinBreakTowerA.gltf";
			//中に入るタイプのモデルのオクル―ダーメッシュはまだできていないのでとりあえずfalse
			modelDesc.buildOccluders = false;
			existingModel = modelAssetMgr->Add(modelDesc, ruinBreakTowerModelHandle);


			ModelAssetHandle ruinStoneModelHandle;
			modelDesc.instancesPeak = 10;
			modelDesc.viewMax = 200.0f;
			modelDesc.pso = normalMapPSOHandle;
			modelDesc.path = "assets/model/Ruins/StoneA/RuinStoneA.gltf";
			modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを
			modelDesc.buildOccluders = true;
			modelAssetMgr->Add(modelDesc, ruinStoneModelHandle);

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
					// scaleを10分の1単位で丸め込む
					// 既存のコリジョンデータを流用するため
					scale *= 10.0f;
					scale.x = std::floor(scale.x) / 10.0f;
					scale.y = std::floor(scale.y) / 10.0f;
					scale.z = std::floor(scale.z) / 10.0f;

					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_1.chullbin", true, scale);
				};
			makeShapeHandleFunc[RockB] = [&](Math::Vec3f scale)
				{
					scale *= 10.0f;
					scale.x = std::floor(scale.x) / 10.0f;
					scale.y = std::floor(scale.y) / 10.0f;
					scale.z = std::floor(scale.z) / 10.0f;
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_2.chullbin", true, scale);
				};
			makeShapeHandleFunc[RockC] = [&](Math::Vec3f scale)
				{
					scale *= 10.0f;
					scale.x = std::floor(scale.x) / 10.0f;
					scale.y = std::floor(scale.y) / 10.0f;
					scale.z = std::floor(scale.z) / 10.0f;
					return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_3.chullbin", true, scale);
				};

			makeShapeHandleFunc[TreeA] = [&](Math::Vec3f scale)
				{
					Physics::ShapeCreateDesc shapeDesc;
					shapeDesc.shape = Physics::CapsuleDesc{ 10.0f,0.5f };
					shapeDesc.localOffset.y = 10.0f;
					return ps->MakeShape(shapeDesc);
				};

			Graphics::PointLightDesc(*pMakePointLightDescFunc[BiomeObjectCount])(Math::Vec3f) = { nullptr };
			pMakePointLightDescFunc[LightFlower] = [](Math::Vec3f location) {
				Graphics::PointLightDesc plDesc;
				plDesc.positionWS = location + Math::Vec3f(0.0f, 3.0f, 0.0f); // 少し上にずらす
				plDesc.color = { 0.2f,0.2f,1.0f }; // 青色
				plDesc.intensity = 1.0f;
				plDesc.range = 10.0f;
				plDesc.castsShadow = false;
				return plDesc;
				};

			bool enableOutline[BiomeObjectCount] = { false };
			enableOutline[RockA] = true;
			enableOutline[RockB] = true;
			enableOutline[RockC] = true;
			enableOutline[TreeA] = true;

			clock_t end = clock();

			const double time = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000.0;
			printf("create entity time %lf[ms]\n", time);


			Math::AABB3f grassBounds;
			std::vector<Math::Vec2f> grassAnchor;
			{
				auto data = modelAssetMgr->Get(grassModelHandle);
				for(auto& mesh : data.ref().subMeshes)
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
							CColor{ {1.0f,1.0f,1.0f,1.0f} }
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
			uint16_t biomeData[4][4] = {
				{0, 0, 0, 0},
				{0, 1, 0, 0},
				{0, 1, 1, 0},
				{0, 0, 1, 0}
			};

			scatterInputs.biomeId.data = &biomeData[0][0];
			scatterInputs.biomeId.w = 4;
			scatterInputs.biomeId.h = 4;
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

			scatterInputs.globalSeed = 12345;

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

				forest.branches = { trees, rocks };
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
				trees.models = { {TreeA, 1.0f} }; //モデルのバリエーションを増やす場合はここに書く

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
				Math::Vec3f location = inst.positionWS;
				Math::Quatf rot = Math::Quatf::FromAxisAngle({ 0,1,0 }, inst.yaw);

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
					if(pMakePointLightDescFunc[modelIdx] != nullptr)
					{
						auto plDesc = pMakePointLightDescFunc[modelIdx](location);
						auto plHandle = pointLightService->Create(plDesc);

						// 静的境界エンティティとして登録
						id = levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ inst.positionWS, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {inst.tint, 1.0f} },
							staticBody,
							CPointLight{ plHandle }
#ifdef _ENABLE_IMGUI
							,shapeDims.value()
#endif
						);
					}
					else {
						// 静的境界エンティティとして登録
						id = levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ inst.positionWS, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {inst.tint, 1.0f} },
							staticBody
#ifdef _ENABLE_IMGUI
							,shapeDims.value()
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
							CColor{ {inst.tint, 1.0f} },
							CPointLight{ plHandle }
						);
					}
					else {
						levelSession.AddStaticBoundsEntity(
							boundsWS,
							CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
							modelComp,
							CColor{ {inst.tint, 1.0f} }
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

			auto addGlobalEntityWithBody = [&](const Math::Vec2f posUV, float offsetY, Graphics::ModelAssetHandle modelHandle, Physics::ShapeHandle shapeHandle, Math::Quatf rot = { 0.0f,0.0f,0.0f,1.0f }, Math::Vec3f scale = { 1.0f,1.0f,1.0f })
				{
					auto location = getTerrainLocation(posUV.x, posUV.y);
					location.y += offsetY;

#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shapeHandle);
#endif
					CModel modelComp{ modelHandle };
					modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

					auto tf = CTransform{ location , rot , scale };

					auto id = levelSession.AddGlobalEntity(
						tf,
						modelComp,
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

			// 塔生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Ruins/Tower/RuinTower.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.7f, 0.7f }, -10.0f, ruinTowerModelHandle, shape);
			}

			// 壊れた塔生成
			{
				auto shape = ps->MakeMesh("generated/meshshape/Ruins/BreakTower/RuinBreakTowerA.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.4f, 0.62f }, -4.0f, ruinBreakTowerModelHandle, shape);
			}

			//石碑生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/Ruins/StoneA/RuinStoneA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.35f, 0.26f }, -4.0f, ruinStoneModelHandle, shape);
			}

			//岩クラスター生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/Static/ClusterRock/ClusterRockA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.28f, 0.24f }, 5.0f, clusterRockModelHandle[0], shape, Math::Quatf::FromEuler(0.0f, Math::Deg2Rad(90.0f), 0.0f));

				shape = ps->MakeConvexCompound("generated/convex/Static/ClusterRock/ClusterRockB.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.5f, 0.2f }, 5.0f, clusterRockModelHandle[1], shape, Math::Quatf::FromEuler(0.0f, Math::Deg2Rad(40.0f), 0.0f));
			}

			//ランドマーククリスタル生成
			{
				auto shape = ps->MakeConvexCompound("generated/convex/landmark/crystals/hugeCrystal.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
				addGlobalEntityWithBody({ 0.8f, 0.8f }, -10.0f, landmarkCrystalModelHandle[0], shape);

				Graphics::PointLightDesc plDesc;
				plDesc.positionWS = getTerrainLocation(0.8f, 0.8f) + Math::Vec3f{ 0.0f,100.0f,0.0 };
				plDesc.intensity = 10.0f;
				plDesc.range = 300.0f;
				plDesc.color = { 1.0f, 0.2f, 1.0f };
				auto plHandle = pointLightService->Create(plDesc);
				levelSession.AddGlobalEntity(
					CPointLight{ plHandle }
				);
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
				Math::Vec3f location = getTerrainLocation(0.26f, 0.2f);
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
			scheduler.AddSystem<PlayerSystem>(*serviceLocator);
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
