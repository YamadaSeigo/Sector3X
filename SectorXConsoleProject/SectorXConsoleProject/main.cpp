
#include "SectorFW/inc/sector11fw.h"
#include "SectorFW/inc/WindowHandler.h"
#include "SectorFW/inc/DX11Graphics.h"

#define WINDOW_NAME "SectorX Console Project"

#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

constexpr uint32_t WINDOW_WIDTH = 720;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 540;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限

using namespace SectorFW;
using namespace SectorFW::ECS;


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

struct Player
{
	Velocity velocity;
	Position position;
	DEFINE_SOA(Player, velocity, position)
};

struct Health { SPARSE_TAG; int hp = 0; };

template<typename Partition>
class MovementSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<Velocity>, Write<Position>>,
	ServiceContext<>>{

	using Accessor = ComponentAccessor<Read<Velocity>, Write<Position>>;

public:
	void UpdateImpl(Partition& partition) override {

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

class AssetService : public IUpdateService
{
public:
	void Update(double deltaTime) override {
		// AssetServiceの更新処理
		//LOG_INFO("AssetService Update: %f", deltaTime);
	}

	STATIC_SERVICE_TAG
	int assetCount = 0;
};

template<typename Partition>
class HealthRegenSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Write<Player>>,
	ServiceContext<AssetService, Graphics::RenderService>>{

	using Accessor = ComponentAccessor<Write<Player>>;
public:

	void UpdateImpl(Partition& partition, UndeletablePtr<AssetService> assetService, UndeletablePtr<Graphics::RenderService> renderService) override {

		/*auto queueView = renderService->GetQueueView("GBuffer");

		Graphics::DrawCommand cmd;
		queueView.PushCommand(cmd);*/

		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount)
			{
				auto player = accessor.Get<Write<Player>>();
				auto velocity = player->velocity();
				auto position = player->position();

				for (size_t i = 0; i < entityCount; ++i) {
					auto vx = velocity.vx()[i];
					auto vy = velocity.vy()[i];
					auto vz = velocity.vz()[i];

					auto x = position.x()[i];
					auto y = position.y()[i];
					auto z = position.z()[i];
				}

			}, partition);

		//assetService->assetCount++;
		//LOG_INFO("Asset Count: %d", assetService->assetCount);
	}
};

struct CModel
{
	Graphics::ModelAssetHandle handle;
};

template<typename Partition>
class SampleSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<TransformSoA>,Read<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService>>{//受け取るサービスの指定

	using Accessor = ComponentAccessor<Read<TransformSoA>,Read<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService) override {
		//機能を制限したRenderQueueを取得
		auto queueLimited = renderService->GetQueueLimited(0);
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount, auto modelMgr, auto queue)
			{
				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Read<CModel>>();
				for (size_t i = 0; i < entityCount; ++i) {
					Math::Vec3f pos = { transform->px()[i],transform->py()[i],transform->pz()[i] };
					auto worldMtx = Math::MakeTranslationMatrix(pos);

					//モデルアセットを取得
					Graphics::DX11ModelAssetData modelAsset = modelMgr->Get(model.value()[i].handle);
					for (const auto& mesh : modelAsset.subMeshes) {
						Graphics::DrawCommand cmd;
						cmd.mesh = mesh.mesh;
						cmd.material = mesh.material;
						cmd.pso = mesh.pso;
						cmd.instance.worldMtx = worldMtx;
						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, mesh.mesh.index);
						queue.PushCommand(std::move(cmd));
					}
				}
			}, partition, modelManager, queueLimited);
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
	ComponentTypeRegistry::Register<Health>();
	ComponentTypeRegistry::Register<Player>();
	ComponentTypeRegistry::Register<CModel>();
	ComponentTypeRegistry::Register<TransformSoA>();
	//========================================================

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	Graphics::DX11GraphicsDevice graphics;
	graphics.Configure(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT);

	AssetService assetService;

	ECS::ServiceLocator serviceLocator(graphics.GetRenderService());
	serviceLocator.Init<AssetService>();

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SectorFW::Graphics;

	graphics.TestInitialize();
	auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11ShaderManager>();
	DX11ShaderCreateDesc shaderDesc;
	shaderDesc.templateID = MaterialTemplateID::PBR;
	shaderDesc.vsPath = L"asset/shader/VS_Default.cso";
	shaderDesc.psPath = L"asset/shader/PS_Default.cso";
	auto shaderHandle = shaderMgr->Add(shaderDesc);

	DX11PSOCreateDesc psoDesc = {shaderHandle};
	auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11PSOManager>();
	auto psoHandle = psoMgr->Add(psoDesc);

	auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11ModelAssetManager>();
	// モデルアセットの読み込み
	DX11ModelAssetCreateDesc modelDesc;
	modelDesc.path = "asset/model/Houseplant.glb";
	modelDesc.shader = shaderHandle;
	modelDesc.pso = psoHandle;
	auto modelAssetHandle = modelAssetMgr->Add(modelDesc);

	//========================================================================================-

	World<Grid2DPartition> world(std::move(serviceLocator));

	for (int i = 0; i < 1; ++i) {
		auto level = std::unique_ptr<Level<Grid2DPartition>>(new Level<Grid2DPartition>("Level" + std::to_string(i)));


		// Entity生成
		for (int j = 0; j < 1; ++j) {
			auto id = level->AddEntity(
				/*Player{
					Velocity{ static_cast<float>(j), static_cast<float>(j), 0.0f },
					Position{ static_cast<float>(j * 10), static_cast<float>(j * 100), 0.0f }
				},*/
				TransformSoA{ 0.0f,0.0f, 0.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
				CModel{ modelAssetHandle }
			);
		}

		// System登録
		auto& scheduler = level->GetScheduler();
		//scheduler.AddSystem<MovementSystem>(world.GetServiceLocator());

		scheduler.AddSystem<SampleSystem>(world.GetServiceLocator());

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