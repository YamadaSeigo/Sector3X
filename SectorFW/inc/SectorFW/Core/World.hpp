/*****************************************************************//**
 * @file   World.hpp
 * @brief ワールドクラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "Level.hpp"
#include "../Util/TypeChecker.hpp"

namespace SFW
{
	/**
	 * @brief 各レベルを管理し、更新するクラス。サービスロケーターを保持する。
	 */
	template<typename... LevelTypes>
	class World {

		template<typename T>
		using LevelLoadingFunc = std::function<void(World<LevelTypes...>*, Level<T>*)>;

		template<typename T>
		struct LevelHolder
		{
			std::unique_ptr<Level<T>> level;
			LevelLoadingFunc<T> loadingFunc;
		};

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
		void AddLevel(std::unique_ptr<Level<T>> level) {

			static_assert(OneOf<T, LevelTypes...>, "指定されていないレベルの分割クラスです");

			auto& vec = std::get<std::vector<LevelHolder<T>>>(levelSets);
			vec.emplace_back(LevelHolder<T>{
				.level = std::move(level),
				.loadingFunc = [](World<LevelTypes...>*, Level<T>*) {}
			});
		}
		/**
		 * @brief レベルを追加する関数
		 * @param level 追加するレベルの右辺値参照
		 * @param loadingFunc レベルのロード時に呼び出される関数
		 */
		template<typename T>
		void AddLevel(std::unique_ptr<Level<T>> level, LevelLoadingFunc<T>&& loadingFunc) {

			static_assert(OneOf<T, LevelTypes...>, "指定されていないレベルの分割クラスです");

			auto& vec = std::get<std::vector<LevelHolder<T>>>(levelSets);
			vec.emplace_back(LevelHolder<T>{
				.level = std::move(level),
				.loadingFunc = std::move(loadingFunc)
			});
		}


		/**
		 * @brief 指定したレベルのロード関数を呼び出す関数
		 * @param levelName ロードするレベルの名前
		 * @details 全レベルの名前と比較して一致するものを探す
		 */
		void LoadLevel(const std::string levelName)
		{
#ifdef _DEBUG
			bool find = false;
#endif

			// 指定された名前のレベルをすべてロードする
			auto loadFunc = [&](auto& vecs)
				{
					for (auto& holder : vecs)
					{
						auto& level = holder.level;
						if (level->GetName() == levelName) {
#ifdef _DEBUG
							find = true;
#endif
							holder.loadingFunc(this, level.get());
						}
					}
				};

			std::apply([&](auto&... levelVecs)
				{
					(..., loadFunc(levelVecs));
				}, levelSets);

#ifdef _DEBUG
			if (!find) LOG_WARNING("指定されたレベルが見つかりませんでした {%s}", levelName.c_str());
#endif
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
			std::vector<std::function<void(ECS::ServiceLocator&, double, IThreadExecutor*)>> mainLevelFunc;
			std::vector<std::function<void(ECS::ServiceLocator&, double, IThreadExecutor*)>> subLevelFunc;

			std::apply([&](auto&... levelVecs)
				{
					(..., [&](auto& vecs, auto& mainFunc, auto& subFunc) {
						for (auto& holder : vecs) {
							auto& level = holder.level;
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
				// コンテナの要素 f をタスク用に move
				auto task = std::move(f);

				executor->Submit(
					[task = std::move(task),        // 「ラムダへの move キャプチャ」
					this,
					deltaTime,
					executor,
					&latch]() mutable
					{
						task(serviceLocator, deltaTime, executor);
						latch.CountDown();
					}
				);
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
		std::tuple<std::vector<LevelHolder<LevelTypes>>...> levelSets;
		//std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
	};
}