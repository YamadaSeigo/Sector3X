#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/DebugRenderSystem.h"
#include "system/TestMoveSystem.h"
#include "system/CleanModelSystem.h"
#include "system/SimpleModelRenderSystem.h"
#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

#define WINDOW_NAME "SectorX Console Project"

constexpr uint32_t WINDOW_WIDTH = 960;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 720;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限


int main(void)
{
	LOG_INFO("SectorX Console Project started");

	//==コンポーネントの登録=====================================
	//main.cppに集めた方がコンパイル効率がいいので、ここで登録している
	//※複数人で開発する場合は、各自のコンポーネントを別ファイルに分けて登録するようにする
	ComponentTypeRegistry::Register<CModel>();
	ComponentTypeRegistry::Register<TransformSoA>();
	ComponentTypeRegistry::Register<SpatialMotionTag>();
	ComponentTypeRegistry::Register<Physics::BodyComponent>();
	ComponentTypeRegistry::Register<Physics::PhysicsInterpolation>();
	ComponentTypeRegistry::Register<Physics::ShapeDims>();
	//========================================================

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	Graphics::DX11GraphicsDevice graphics;
	graphics.Configure<Debug::ImGuiBackendDX11Win32>(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT, FPS_LIMIT);

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
	Physics::PhysicsService::Plan physicsPlan = { 1.0f / (float)FPS_LIMIT, 1, false };
	Physics::PhysicsService physicsService(physics, shapeManager, physicsPlan);

	Input::WinInput winInput(WindowHandler::GetMouseInput());
	InputService* inputService = &winInput;

	auto bufferMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11BufferManager>();
	Graphics::DX113DPerCameraService dx11PerCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I3DPerCameraService* perCameraService = &dx11PerCameraService;

	Graphics::DX113DOrtCameraService dx11OrtCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I3DOrtCameraService* ortCameraService = &dx11OrtCameraService;

	Graphics::DX112DCameraService dx112DCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I2DCameraService* camera2DService = &dx112DCameraService;

	ECS::ServiceLocator serviceLocator(graphics.GetRenderService(), &physicsService, inputService, perCameraService, ortCameraService, camera2DService);
	serviceLocator.InitAndRegisterStaticService<SpatialChunkRegistry>();


	//デバッグ用の初期化
	//========================================================================================-
	using namespace SectorFW::Graphics;

	graphics.TestInitialize();
	auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11ShaderManager>();
	DX11ShaderCreateDesc shaderDesc;
	shaderDesc.templateID = MaterialTemplateID::PBR;
	shaderDesc.vsPath = L"assets/shader/VS_Default.cso";
	shaderDesc.psPath = L"assets/shader/PS_Default.cso";
	ShaderHandle shaderHandle;
	shaderMgr->Add(shaderDesc, shaderHandle);

	DX11PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
	auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11PSOManager>();
	PSOHandle psoHandle;
	psoMgr->Add(psoDesc, psoHandle);

	ModelAssetHandle modelAssetHandle[4];

	auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11ModelAssetManager>();
	// モデルアセットの読み込み
	DX11ModelAssetCreateDesc modelDesc;
	modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_1.gltf";
	modelDesc.pso = psoHandle;
	modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
	modelDesc.instancesPeak = 1000;
	modelDesc.viewMax = 1000.0f;

	modelAssetMgr->Add(modelDesc, modelAssetHandle[0]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/Clover_1.gltf";
	modelAssetMgr->Add(modelDesc, modelAssetHandle[1]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/DeadTree_1.gltf";
	modelDesc.buildOccluders = false;
	modelAssetMgr->Add(modelDesc, modelAssetHandle[2]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/Grass_Common_Tall.gltf";
	modelAssetMgr->Add(modelDesc, modelAssetHandle[3]);


	//========================================================================================-

	World<Grid2DPartition, Grid3DPartition, QuadTreePartition, OctreePartition> world(std::move(serviceLocator));
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	std::random_device rd;
	std::mt19937_64 rng(rd());

	// 例: A=50%, B=30%, C=20% のつもりで重みを設定（整数でも実数でもOK）
	std::array<int, 4> weights{ 20, 30, 10 ,50};
	std::discrete_distribution<int> dist(weights.begin(), weights.end());

	for (int i = 0; i < 1; ++i) {
		auto level = std::unique_ptr<Level<OctreePartition>>(new Level<OctreePartition>("Level" + std::to_string(i), *entityManagerReg, ELevelState::Main));

		// System登録
		auto& scheduler = level->GetScheduler();

		scheduler.AddSystem<ModelRenderSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<SimpleModelRenderSystem>(world.GetServiceLocator());
		scheduler.AddSystem<CameraSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<TestMoveSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<PhysicsSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<BuildBodiesFromIntentsSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(world.GetServiceLocator());
		scheduler.AddSystem<DebugRenderSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<CleanModelSystem>(world.GetServiceLocator());

		auto ps = world.GetServiceLocator().Get<Physics::PhysicsService>();
		auto sphere = ps->MakeSphere(0.5f);//ps->MakeBox({ 0.5f, 0.5f, 0.5f }); // Box形状を生成
		auto sphereDims = ps->GetShapeDims(sphere);

		auto box = ps->MakeBox({ 1000.0f,0.5f, 1000.0f });
		auto boxDims = ps->GetShapeDims(box);

		Math::Vec3f src = { 0.0f,50.0f,0.0f };
		Math::Vec3f dst = src;

		// Entity生成
		for (int j = 0; j < 20; ++j) {
			for (int k = 0; k < 20; ++k) {
				for (int n = 0; n < 1; ++n) {
					Math::Vec3f location = { float(rand() % 400 + 1), float(n) * 20.0f, float(rand() % 400 + 1) };
					//Math::Vec3f location = { 10.0f * j,0.0f,10.0f * k };
					auto chunk = level->GetChunk(location);
					auto key = chunk.value()->GetNodeKey();
					SpatialMotionTag tag{};
					tag.handle = { key, chunk.value() };
					float scale = 1.0f + float(rand() % 100 - 50) / 100.0f;
					//float scale = 1.0f;
					auto id = level->AddEntity(
						TransformSoA{ location, Math::Quatf(0.0f,0.0f,0.0f,1.0f),Math::Vec3f(scale,scale,scale) },
						CModel{ modelAssetHandle[dist(rng)] },
						Physics::BodyComponent{},
						Physics::PhysicsInterpolation(
							location, // 初期位置
							Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
						),
						sphereDims.value(),
						tag
					);
					/*if (id) {
						ps->EnqueueCreateIntent(id.value(), sphere, key);
					}*/
				}
			}
		}
		//Physics::BodyComponent staticBody{};
		//staticBody.isStatic = Physics::BodyType::Static; // staticにする
		//auto id = level->AddEntity(
		//	TransformSoA{ 10.0f,-10.0f, 10.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
		//	CModel{ modelAssetHandle },
		//	staticBody,
		//	Physics::PhysicsInterpolation(
		//		Math::Vec3f{ 10.0f,-10.0f, 10.0f }, // 初期位置
		//		Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
		//	),
		//	boxDims.value()
		//);
		//if (id) {
		//	auto chunk = level->GetChunk({ 0.0f,-100.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
		//	ps->EnqueueCreateIntent(id.value(), box, chunk.value()->GetNodeKey());
		//}

		world.AddLevel(std::move(level));
	}

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop();
		});

	return WindowHandler::Destroy();
}