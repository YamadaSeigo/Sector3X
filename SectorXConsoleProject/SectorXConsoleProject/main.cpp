
#include "SectorFW/inc/sector11fw.h"
#include "SectorFW/inc/WindowHandler.h"
#include "SectorFW/inc/DX11Graphics.h"

#define WINDOW_NAME "SectorX Console Project"

#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

constexpr uint32_t WINDOW_WIDTH = 720;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 540;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 10.0;	// フレームレート制限

using namespace SectorFW;
using namespace SectorFW::ECS;

//----------------------------------------------
// Example System using the Factory
//----------------------------------------------
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


template<typename Partition>
class HealthRegenSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<Health>,Write<Health>>,
	ServiceContext<>>{
public:

	void UpdateImpl(Partition& partition) override {
	}
};

int main(void)
{
	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	auto graphics = std::make_unique<DX11GraphicsDevice>();
	graphics->Configure(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT);

	ComponentTypeRegistry::Register<Transform>();
	ComponentTypeRegistry::Register<Velocity>();
	ComponentTypeRegistry::Register<Position>();
	ComponentTypeRegistry::Register<Health>();

	World<Grid2DPartition> world;

	for (int i = 0; i < 1; ++i) {
		auto level = std::unique_ptr<Level<Grid2DPartition>>(new Level<Grid2DPartition>("Level" + std::to_string(i)));
		auto& scheduler = level->GetScheduler();

		// Entity生成
		for (int j = 0; j < 100; ++j) {
			auto id = level->AddEntity(
				Position{ float(0),float(0) },
				Velocity{ float(10),float(10) });
		}

		// System登録
		scheduler.AddSystem<MovementSystem>();
		scheduler.AddSystem<HealthRegenSystem>();

		world.AddLevel(std::move(level));
	}

	LOG_INFO("SectorX Console Project started");

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く

		gameEngine.MainLoop();

		});

	return WindowHandler::Destroy();
}