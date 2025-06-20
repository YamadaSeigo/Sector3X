#pragma once

#include "Level.h"
#include "AssetManager.h"

namespace SectorFW
{
	template<typename... LevelTypes>
	class World {

	public:
		template<typename T>
		void AddLevel(std::unique_ptr<Level<T>>&& level) {
			std::get<std::vector<std::unique_ptr<Level<T>>>>(levelSets).push_back(std::move(level));
		}

		void UpdateAllLevels() {
			static std::vector<std::future<void>> futures;
			std::apply([](auto&... levelVecs)
				{
					(..., [&]() {
						for (auto& level : levelVecs) {
							if (level->GetState() == ELevelState::Main) {
								futures.emplace_back(std::async(std::launch::async, [&]() {
									level->Update();
									}));
							}
							else if (level->GetState() == ELevelState::Sub) {
								futures.emplace_back(std::async(std::launch::async, [&]() {
									level->UpdateLimited();
									}));
							}
						}
						}());
				}, levelSets);

			for (auto& f : futures) f.get();
			futures.clear();
		}

	private:
		std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		AssetManager assetManager;
	};
}
