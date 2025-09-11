#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/ShapeDimsRenderSystem.h"
#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

#define WINDOW_NAME "SectorX Console Project"

constexpr uint32_t WINDOW_WIDTH = 960;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 720;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限

struct Velocity
{
	float vx = 0, vy = 0, vz = 0;

	DEFINE_SOA(Velocity, vx, vy, vz)
};

struct Position
{
	float x = 0, y = 0, z = 0;

	DEFINE_SOA(Position, x, y, z)
};

struct Health { SPARSE_TAG; int hp = 0; };

template<typename Partition>
class MovementSystem : public ITypeSystem<
	MovementSystem<Partition>,
	Partition,
	ComponentAccess<Read<Velocity>, Write<Position>>,
	ServiceContext<>>{
	using Accessor = ComponentAccessor<Read<Velocity>, Write<Position>>;

public:
	void UpdateImpl(Partition& partition) {
		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount)
			{
				auto velocityPtr = accessor.Get<Read<Velocity>>();
				if (!velocityPtr) return;

				auto positionPtr = accessor.Get<Write<Position>>();
				if (!positionPtr) return;

				auto p_x = positionPtr->x();
				auto p_y = positionPtr->y();
				auto p_z = positionPtr->z();

				auto p_vx = velocityPtr->vx();
				auto p_vy = velocityPtr->vy();
				auto p_vz = velocityPtr->vz();

				size_t i = 0;
				size_t simdWidth = 8; // AVX: 256bit / 32bit = 8 floats

				float dt = 0.01f;

				__m256 dt_vec = _mm256_set1_ps(dt);

				for (; i + simdWidth <= entityCount; i += simdWidth) {
					// load
					__m256 px = _mm256_loadu_ps(p_x + i);
					__m256 py = _mm256_loadu_ps(p_y + i);
					__m256 pz = _mm256_loadu_ps(p_z + i);

					__m256 vx = _mm256_loadu_ps(p_vx + i);
					__m256 vy = _mm256_loadu_ps(p_vy + i);
					__m256 vz = _mm256_loadu_ps(p_vz + i);

					// v * dt
					vx = _mm256_mul_ps(vx, dt_vec);
					vy = _mm256_mul_ps(vy, dt_vec);
					vz = _mm256_mul_ps(vz, dt_vec);

					// p += v * dt
					px = _mm256_add_ps(px, vx);
					py = _mm256_add_ps(py, vy);
					pz = _mm256_add_ps(pz, vz);

					// store
					_mm256_storeu_ps(p_x + i, px);
					_mm256_storeu_ps(p_y + i, py);
					_mm256_storeu_ps(p_z + i, pz);
				}

				// 残りをスカラー処理
				for (; i < entityCount; ++i) {
					p_x[i] += p_vx[i] * dt;
					p_y[i] += p_vy[i] * dt;
					p_z[i] += p_vz[i] * dt;
				}

				for (size_t i = 0; i < entityCount; ++i) {
					/*velocityPtr.value()[i].vx += 1.0f;
					LOG_INFO("%f", velocityPtr.value()[i].vx);*/

					LOG_INFO("Velocity: (%f, %f, %d)", p_x[i], p_y[i], p_z[i]);
				}
			}, partition);
	}
};

int main(void)
{
	LOG_INFO("SectorX Console Project started");

	//==コンポーネントの登録=====================================
	//main.cppに集めた方がコンパイル効率がいいので、ここで登録している
	//※複数人で開発する場合は、各自のコンポーネントを別ファイルに分けて登録するようにする
	ComponentTypeRegistry::Register<Transform>();
	ComponentTypeRegistry::Register<Velocity>();
	ComponentTypeRegistry::Register<Position>();
	ComponentTypeRegistry::Register<CModel>();
	ComponentTypeRegistry::Register<TransformSoA>();
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
	params.maxContactConstraints = 1 * 1024;
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
	Graphics::DX113DCameraService dx11CameraService(bufferMgr);
	Graphics::I3DCameraService* cameraService = &dx11CameraService;

	ECS::ServiceLocator serviceLocator(graphics.GetRenderService(), &physicsService, inputService, cameraService);
	serviceLocator.InitAndRegisterStaticService<EntityManagerRegistry>();

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SectorFW::Graphics;

	graphics.TestInitialize();
	auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11ShaderManager>();
	DX11ShaderCreateDesc shaderDesc;
	shaderDesc.templateID = MaterialTemplateID::PBR;
	shaderDesc.vsPath = L"asset/shader/VS_Default.cso";
	shaderDesc.psPath = L"asset/shader/PS_Default.cso";
	ShaderHandle shaderHandle;
	shaderMgr->Add(shaderDesc, shaderHandle);

	DX11PSOCreateDesc psoDesc = { shaderHandle };
	auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11PSOManager>();
	PSOHandle psoHandle;
	psoMgr->Add(psoDesc, psoHandle);

	auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11ModelAssetManager>();
	// モデルアセットの読み込み
	DX11ModelAssetCreateDesc modelDesc;
	modelDesc.path = "asset/model/Cubone.glb";
	modelDesc.shader = shaderHandle;
	modelDesc.pso = psoHandle;
	modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
	ModelAssetHandle modelAssetHandle;
	modelAssetMgr->Add(modelDesc, modelAssetHandle);

	//========================================================================================-

	World<Grid2DPartition, Grid3DPartition, QuadTreePartition, OctreePartition> world(std::move(serviceLocator));
	auto entityManagerReg = world.GetServiceLocator().Get<EntityManagerRegistry>();

	for (int i = 0; i < 1; ++i) {
		auto level = std::unique_ptr<Level<Grid3DPartition>>(new Level<Grid3DPartition>("Level" + std::to_string(i), ELevelState::Main,
			(ChunkSizeType)4, (ChunkSizeType)4,(ChunkSizeType)4, 64.0f));

		// System登録
		auto& scheduler = level->GetScheduler();
		//scheduler.AddSystem<MovementSystem>(world.GetServiceLocator());

		scheduler.AddSystem<ModelRenderSystem>(world.GetServiceLocator());
		scheduler.AddSystem<CameraSystem>(world.GetServiceLocator());
		scheduler.AddSystem<PhysicsSystem>(world.GetServiceLocator());
		scheduler.AddSystem<BuildBodiesFromIntentsSystem>(world.GetServiceLocator());
		scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(world.GetServiceLocator());
		scheduler.AddSystem<ShapeDimsRenderSystem>(world.GetServiceLocator());

		auto ps = world.GetServiceLocator().Get<Physics::PhysicsService>();
		auto sphere = ps->MakeSphere(0.5f);//ps->MakeBox({ 0.5f, 0.5f, 0.5f }); // Box形状を生成
		auto sphereDims = ps->GetShapeDims(sphere);

		auto box = ps->MakeBox({ 10.0f,0.5f, 10.0f });
		auto boxDims = ps->GetShapeDims(box);

		// Entity生成
		for (int j = 0; j < 10; ++j) {
			for (int k = 0; k < 10; ++k) {
				for (int n = 0; n < 1; ++n) {
					Math::Vec3f location = { powf(float(j),3), float(n) * 2.0f, powf(float(k),3) };
					auto id = level->AddEntity(
						TransformSoA{ location, Math::Quatf(0.0f,0.0f,0.0f,1.0f),Math::Vec3f(1.0f,1.0f,1.0f) },
						CModel{ modelAssetHandle },
						Physics::BodyComponent{},
						Physics::PhysicsInterpolation(
							location, // 初期位置
							Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
						),
						sphereDims.value()
					);
					/*if (id) {
						auto chunk = level->GetChunk(location);
						ps->EnqueueCreateIntent(id.value(), sphere, chunk.value()->GetNodeKey());
					}*/
				}
			}
		}
		Physics::BodyComponent staticBody{};
		staticBody.isStatic = Physics::BodyType::Static; // staticにする
		auto id = level->AddEntity(
			TransformSoA{ 10.0f,-10.0f, 10.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
			CModel{ modelAssetHandle },
			staticBody,
			Physics::PhysicsInterpolation(
				Math::Vec3f{ 10.0f,-10.0f, 10.0f }, // 初期位置
				Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
			),
			boxDims.value()
		);
		if (id) {
			auto chunk = level->GetChunk({ 0.0f,-100.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
			ps->EnqueueCreateIntent(id.value(), box, chunk.value()->GetNodeKey());
		}

		world.AddLevel(std::move(level), *entityManagerReg);
	}

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop();
		});

	return WindowHandler::Destroy();
}