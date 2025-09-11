#pragma once

#include "Level.hpp"

namespace SectorFW
{
	template<typename... LevelTypes>
	class World {
	public:
		explicit World(ECS::ServiceLocator&& serviceLocator) noexcept
			: serviceLocator(std::move(serviceLocator)) {
		}

		// ムーブコンストラクタ
		World(World&& other) noexcept
			: levelSets(std::move(other.levelSets)),
			serviceLocator(std::move(other.serviceLocator)) {
		}

		// ムーブ代入演算子
		World& operator=(World&& other) noexcept {
			if (this != &other) {
				levelSets = std::move(other.levelSets);
				serviceLocator = std::move(other.serviceLocator);
			}
			return *this;
		}

		template<typename T>
		void AddLevel(std::unique_ptr<Level<T>>&& level, EntityManagerRegistry& reg) {
			level->RegisterAllChunks(reg);
			std::get<std::vector<std::unique_ptr<Level<T>>>>(levelSets).push_back(std::move(level));
		}

		void UpdateAllLevels(double deltaTime) {
#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();
				frame.items.clear();

				// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::World, /*leaf=*/false, "World" });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			static std::vector<std::future<void>> futures;
			std::apply([&](auto&... levelVecs)
				{
					(..., [](decltype(levelVecs)& vecs, const ECS::ServiceLocator& locator, double delta) {
						for (auto& level : vecs) {
							if (level->GetState() == ELevelState::Main) {
								futures.emplace_back(std::async(std::launch::async, [&level, &locator, delta]() {
									level->Update(locator, delta);
									}));
							}
							else if (level->GetState() == ELevelState::Sub) {
								futures.emplace_back(std::async(std::launch::async, [&level, &locator, delta]() {
									level->UpdateLimited(locator, delta);
									}));
							}
						}
						}(levelVecs, serviceLocator, deltaTime));
				}, levelSets);

			for (auto& f : futures) f.get();
			futures.clear();
		}

		void UpdateServiceLocator(double deltaTime) {
			serviceLocator.UpdateService(deltaTime);
		}

		const ECS::ServiceLocator& GetServiceLocator() const noexcept {
			return serviceLocator;
		}

	private:
		std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
	};
}