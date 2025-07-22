#pragma once

#include "Level.hpp"
#include "AssetManager.h"

namespace SectorFW
{
	template<typename... LevelTypes>
	class World {
	public:
		// ムーブコンストラクタ
		World(World&& other) noexcept
			: levelSets(std::move(other.levelSets)),
			serviceLocator(std::move(other.serviceLocator)),
			assetManager(std::move(other.assetManager)) {
		}

		// ムーブ代入演算子
		World& operator=(World&& other) noexcept {
			if (this != &other) {
				levelSets = std::move(other.levelSets);
				serviceLocator = std::move(other.serviceLocator);
				assetManager = std::move(other.assetManager);
			}
			return *this;
		}

		explicit World(ECS::ServiceLocator&& serviceLocator) noexcept
			: serviceLocator(std::move(serviceLocator)) {
			assert(this->serviceLocator.IsInitialized() && "ServiceLocator is not already initialized.");
		}

		template<typename T>
		void AddLevel(std::unique_ptr<Level<T>>&& level) {
			std::get<std::vector<std::unique_ptr<Level<T>>>>(levelSets).push_back(std::move(level));
		}

		void UpdateAllLevels() {
			static std::vector<std::future<void>> futures;
			std::apply([&](auto&... levelVecs)
				{
					(..., [](decltype(levelVecs)& vecs, const ECS::ServiceLocator& locator) {
						for (auto& level : vecs) {
							if (level->GetState() == ELevelState::Main) {
								futures.emplace_back(std::async(std::launch::async, [&]() {
									level->Update(locator);
									}));
							}
							else if (level->GetState() == ELevelState::Sub) {
								futures.emplace_back(std::async(std::launch::async, [&]() {
									level->UpdateLimited(locator);
									}));
							}
						}
						}(levelVecs, serviceLocator));
				}, levelSets);

			for (auto& f : futures) f.get();
			futures.clear();
		}

		const ECS::ServiceLocator& GetServiceLocator() const noexcept {
			return serviceLocator;
		}

	private:
		std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
		AssetManager assetManager;
	};
}