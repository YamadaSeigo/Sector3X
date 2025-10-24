/*****************************************************************//**
 * @file   World.hpp
 * @brief ワールドクラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "Level.hpp"

namespace SFW
{
	/**
	 * @brief 各レベルを管理し、更新するクラス。サービスロケーターを保持する。
	 */
	template<typename... LevelTypes>
	class World {
	public:
		/**
		 * @brief コンストラクタ
		 * @param serviceLocator サービスロケーター右辺値参照
		 */
		explicit World(ECS::ServiceLocator&& serviceLocator) noexcept
			: serviceLocator(std::move(serviceLocator)) {
		}

		/**
		 * @brief ムーブコンストラクタ
		 * @param other ムーブ元のWorldオブジェクト
		 */
		World(World&& other) noexcept
			: levelSets(std::move(other.levelSets)),
			serviceLocator(std::move(other.serviceLocator)) {
		}

		/**
		 * @brief ムーブ代入演算子
		 * @param other ムーブ元のWorldオブジェクト
		 * @return World& ムーブ後のWorldオブジェクトへの参照
		 */
		World& operator=(World&& other) noexcept {
			if (this != &other) {
				levelSets = std::move(other.levelSets);
				serviceLocator = std::move(other.serviceLocator);
			}
			return *this;
		}
		/**
		 * @brief レベルを追加する関数
		 * @param level 追加するレベルの右辺値参照
		 */
		template<typename T>
		void AddLevel(std::unique_ptr<Level<T>>&& level) {
			//level->RegisterAllChunks(reg);
			std::get<std::vector<std::unique_ptr<Level<T>>>>(levelSets).push_back(std::move(level));
		}
		/**
		 * @brief すべてのレベルを更新する関数。
		 * @param deltaTime デルタタイム（秒）
		 */
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
		/**
		 * @brief サービスロケーターのサービスを更新する関数
		 * @param deltaTime デルタタイム（秒）
		 */
		void UpdateServiceLocator(double deltaTime) {
			serviceLocator.UpdateService(deltaTime);
		}
		/**
		 * @brief サービスロケーターを取得する関数
		 * @return const ECS::ServiceLocator& サービスロケーターへの定数参照
		 */
		const ECS::ServiceLocator& GetServiceLocator() const noexcept {
			return serviceLocator;
		}
	private:
		std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
	};
}