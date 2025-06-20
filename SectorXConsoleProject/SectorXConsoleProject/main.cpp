
#include "SectorFW/inc/sector11fw.h"
#include "SectorFW/inc/WindowHandler.h"
#include "SectorFW/inc/DX11Graphics.h"

#define WINDOW_NAME "SectorX Console Project"

constexpr uint32_t WINDOW_WIDTH = 720;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 540;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限

using namespace SectorFW;
using namespace SectorFW::ECS;

//----------------------------------------------
// Example System using the Factory
//----------------------------------------------
struct Velocity { float vx = 0, vy = 0; };
struct Health { SPARSE_TAG; int hp = 0; };

template<typename Partition>
class MovementSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<Health>, Write<Health>>,
	ServiceContext<>>{
public:
	AccessInfo GetAccessInfo() const noexcept override {
		AccessInfo info; (RegisterAccessType<Write<Health>>, RegisterAccessType<Read<Health>>(info)); return info;
	}

	void UpdateImpl(Partition& partiton) override {
		/*Query<Partition&> query(partiton);
		query.With<Transform, Velocity>();
		for (ArchetypeChunk* chunk : query.MatchingChunks()) {
			auto transforms = chunk->GetColumn<Transform>();
			auto velocities = chunk->GetColumn<Velocity>();
			auto entityCount = chunk->GetEntityCount();
			for (size_t i = 0; i < entityCount; ++i) {
				transforms[i].location.x += velocities[i].vx;
				transforms[i].location.y += velocities[i].vy;
				std::cout << std::to_string(transforms[i].location.x) << std::endl;
			}
		}*/
	}
};

template<typename Partition>
class HealthRegenSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<Health>,Write<Health>>,
	ServiceContext<>>{
public:

	void UpdateImpl(Partition& partiton) override {
		std::cout << "aa" << std::endl;
	}
};

int main(void)
{
	std::istringstream inputStream;

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	auto graphics = std::make_unique<DX11GraphicsDevice>();
	graphics->Configure(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT);

	ComponentTypeRegistry::Register<Transform>();
	ComponentTypeRegistry::Register<Velocity>();
	ComponentTypeRegistry::Register<Health>();

	World<Grid2DPartition> world;

	for (int i = 0; i < 2; ++i) {
		auto level = std::unique_ptr<Level<Grid2DPartition>>(new Level<Grid2DPartition>("Level" + std::to_string(i)));
		//EntityManager& em = level->GetEntityManager();
		auto& scheduler = level->GetScheduler();

		// Entity生成
		for (int j = 0; j < 10000; ++j) {
			if (j % 2 == 0)
			{
				auto id = level->AddEntity(
					//Transform{ float(j),float(j)},
					Velocity{ 1.0f,0.5f }
					//Health(100)
				);
			}
			else
			{
				auto id = level->AddEntity(
					//Transform{ float(j),float(j)},
					Velocity{ 1.0f,0.5f }
				);
			}
		}

		// System登録
		scheduler.AddSystem<MovementSystem>();
		scheduler.AddSystem<HealthRegenSystem>();

		world.AddLevel(std::move(level));
	}

	LOG_ERROR("SectorX Console Project started");

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く

		gameEngine.MainLoop();

		});

	return WindowHandler::Destroy();
}