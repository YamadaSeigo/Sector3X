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
		void UpdateAllLevels(double deltaTime, IThreadExecutor* executor) {
#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();
				frame.items.clear();

				// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_WORLD, /*leaf=*/false, "World" });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			static std::vector<std::future<void>> futures;
			std::vector<std::function<void(ECS::ServiceLocator&, double, IThreadExecutor*)>> mainLevelFunc;
			std::vector<std::function<void(ECS::ServiceLocator&, double, IThreadExecutor*)>> subLevelFunc;

			std::apply([&](auto&... levelVecs)
				{
					(..., [&](auto& vecs, auto& mainFunc, auto& subFunc) {
						for (auto& level : vecs) {
							if (level->GetState() == ELevelState::Main) {
								mainFunc.push_back([&level](auto& locator, double delta, auto* te) {
									level->Update(locator, delta, te);
									});
							}
							else if (level->GetState() == ELevelState::Sub) {
								subFunc.push_back([&level](auto& locator, double delta, auto* te) {
									level->UpdateLimited(locator, delta, te);
									});
							}
						}
						}(levelVecs, mainLevelFunc, subLevelFunc));
				}, levelSets);

			ThreadCountDownLatch latch((int)mainLevelFunc.size());
			//メインのレベルの更新処理を並行で実行
			for (auto& f : mainLevelFunc)
			{
				executor->Submit([&]() {
					f(serviceLocator, deltaTime, executor);
					latch.CountDown();
					});
			}

			//サブレベルの更新処理実行
			for (auto& f : subLevelFunc) f(serviceLocator, deltaTime, executor);

			latch.Wait();
		}
		/**
		 * @brief サービスロケーターのサービスを更新する関数
		 * @param deltaTime デルタタイム（秒）
		 */
		void UpdateServiceLocator(double deltaTime, IThreadExecutor* executor) {
			serviceLocator.UpdateService(deltaTime, executor);
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