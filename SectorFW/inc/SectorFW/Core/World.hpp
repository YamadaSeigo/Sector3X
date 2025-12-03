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
		using LevelCustomFunc = std::function<void(World<LevelTypes...>*, Level<T>*)>;

	public:
		template<typename T>
		struct LevelHolder
		{
			std::unique_ptr<Level<T>> level;
			LevelCustomFunc<T> loadingFunc;
			LevelCustomFunc<T> cleanFunc;
		};

		struct Session
		{
			Session(World<LevelTypes...>& _world) : world(_world) {}

			/**
		 * @brief レベルを追加する関数
		 * @param level 追加するレベルの右辺値参照
		 */
			template<typename T>
			void AddLevel(std::unique_ptr<Level<T>> level) {

				static_assert(OneOf<T, LevelTypes...>, "指定されていないレベルの分割クラスです");

				auto& vec = std::get<std::vector<LevelHolder<T>>>(world.levelSets);
				vec.emplace_back(LevelHolder<T>{
					.level = std::move(level)
				});
			}
			/**
			 * @brief レベルを追加する関数
			 * @param level 追加するレベルの右辺値参照
			 * @param customFunc レベルのロード時やアンロード時に呼び出される関数
			 */
			template<typename T, typename... Func>
			void AddLevel(std::unique_ptr<Level<T>> level, Func&&... customFunc) {

				static_assert(OneOf<T, LevelTypes...>, "指定されていないレベルの分割クラスです");

				auto& vec = std::get<std::vector<LevelHolder<T>>>(world.levelSets);
				vec.emplace_back(LevelHolder<T>{
					std::move(level),
					std::move(customFunc)...
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
								if (holder.loadingFunc) {
									holder.loadingFunc(&world, level.get());
								}
							}
						}
					};

				std::apply([&](auto&... levelVecs)
					{
						(..., loadFunc(levelVecs));
					}, world.levelSets);

#ifdef _DEBUG
				if (!find) LOG_WARNING("指定されたレベルが見つかりませんでした {%s}", levelName.c_str());
#endif
			}

			void CleanLevel(const std::string levelName)
			{
#ifdef _DEBUG
				bool find = false;
#endif

				// 指定された名前のレベルをすべてロードする
				auto cleanFunc = [&](auto& vecs)
					{
						for (auto& holder : vecs)
						{
							auto& level = holder.level;
							if (level->GetName() == levelName) {
#ifdef _DEBUG
								find = true;
#endif
								level->Clean(world.serviceLocator);
								if (holder.cleanFunc) {
									holder.cleanFunc(&world, level.get());
								}
							}
						}
					};

				std::apply([&](auto&... levelVecs)
					{
						(..., cleanFunc(levelVecs));
					}, world.levelSets);

#ifdef _DEBUG
				if (!find) LOG_WARNING("指定されたレベルが見つかりませんでした {%s}", levelName.c_str());
#endif
			}
		private:
			World<LevelTypes...>& world;
		};

		class IRequestCommand
		{
		public:
			virtual void Execute(Session* pWorldSession) = 0;
		};
		/*
		* @brief Systemなどの下層からWorldに対してのリクエストを受け付ける
		*/
		class RequestService
		{
		public:
			/*
			* @brief コマンドをリクエストに追加する
			*/
			void PushCommand(std::unique_ptr<IRequestCommand> cmd) {
				requests.push_back(std::move(cmd));
			}
			// すべてのコマンドを実行する関数
			// WorldでLevelを更新する前に呼び出す
			void FlashAllCommand(World<LevelTypes...>* pWorld) {

				if (requests.empty()) return;

				auto session = pWorld->GetSession();

				for (auto& cmd : requests) {
					cmd->Execute(&session);
				}

				requests.clear();
			}
		private:
			std::vector<std::unique_ptr<IRequestCommand>> requests;
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
			//下層からリクエストされたコマンドを実行
			requestService.FlashAllCommand(this);

			serviceLocator.UpdateService(deltaTime, executor);
		}
		/**
		 * @brief サービスロケーターを取得する関数
		 * @return const ECS::ServiceLocator& サービスロケーターへの定数参照
		 */
		const ECS::ServiceLocator& GetServiceLocator() const noexcept {
			return serviceLocator;
		}
		/*
		* @brief Worldへのセッション(レベルの追加など)を取得する関数
		* @return Session Worldのセッションオブジェクト
		*/
		[[nodiscard]] Session GetSession() {
			return Session(*this);
		}
	private:
		std::tuple<std::vector<LevelHolder<LevelTypes>>...> levelSets;
		//std::tuple<std::vector<std::unique_ptr<Level<LevelTypes>>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
		RequestService requestService;
	};
}