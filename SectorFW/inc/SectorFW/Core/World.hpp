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
	 * @details コピー不可
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
					std::forward<Func>(customFunc)...
				});
			}

			template<typename T>
			void AddLevel(LevelHolder<T>&& holder) {

				static_assert(OneOf<T, LevelTypes...>, "指定されていないレベルの分割クラスです");

				auto& vec = std::get<std::vector<LevelHolder<T>>>(world.levelSets);
				vec.push_back(std::move(holder));
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

			template<template<typename> class SystemType>
			void AddGlobalSystem() {
				world.globalSystem.AddSystem<SystemType>(world.serviceLocator);
			}

		private:
			World<LevelTypes...>& world;
		};


		/*
		* @brief Worldに対するリクエストコマンドのインターフェース
		*/
		class IRequestCommand
		{
		public:
			virtual ~IRequestCommand() = default;
			virtual void Execute(Session* pWorldSession) = 0;
		};
		/*
		* @brief Worldにレベルを追加するコマンド
		*/
		template<typename T>
		class AddLevelCommand : public IRequestCommand
		{
		public:
			AddLevelCommand(std::unique_ptr<Level<T>> level)
			{
				holder = LevelHolder<T>{
					std::move(level)
				};
			}

			template<typename... Func>
			AddLevelCommand(std::unique_ptr<Level<T>> level, Func&&... customFunc)
			{
				holder = LevelHolder<T>{
					std::move(level),
					std::move(customFunc)...
				};
			}

			void Execute(Session* pWorldSession) override {
				pWorldSession->AddLevel<T>(std::move(holder));
			}

		private:
			LevelHolder<T> holder;
		};

		/*
		* @brief Worldにレベルをロードするコマンド
		*/
		class LoadLevelCommand : public IRequestCommand
		{
		public:
			LoadLevelCommand(const std::string& name) : levelName(name) {}

			void Execute(Session* pWorldSession) override {
				pWorldSession->LoadLevel(levelName);
			}
		private:
			std::string levelName;
		};
		/*
		* @brief Worldにレベルをクリーンするコマンド
		*/
		class CleanLevelCommand : public IRequestCommand
		{
		public:
			CleanLevelCommand(const std::string& name) : levelName(name) {}
			void Execute(Session* pWorldSession) override {
				pWorldSession->CleanLevel(levelName);
			}
		private:
			std::string levelName;
		};

		template<template<typename> class SystemType>
		class AddGlobalSystemCommand : public IRequestCommand
		{
		public:
			AddGlobalSystemCommand() = default;

			void Execute(Session* pWorldSession) override {
				pWorldSession->AddGlobalSystem<SystemType>();
			}
		};

		/*
		* @brief Systemなどの下層からWorldに対してのリクエストを受け付ける
		*/
		class RequestService
		{
			friend class World<LevelTypes...>;
		public:
			RequestService() : requestMutex(std::make_unique<std::mutex>()) {}

			RequestService(RequestService&& other) noexcept :
				requestMutex(other.requestMutex),
				requests(std::move(other.requests)){
			}

			RequestService& operator=(RequestService&& other) {
				if (this != &other) {
					requestMutex = other.requestMutex;
					requests = std::move(other.requests);
				}
				return *this;
			}

			/*
			* @brief コマンドをリクエストに追加する
			*/
			void PushCommand(std::unique_ptr<IRequestCommand> cmd) {
				std::lock_guard<std::mutex> lock(*requestMutex);
				requests.push_back(std::move(cmd));
			}

			template<typename T>
			[[nodiscard]] std::unique_ptr<IRequestCommand> CreateAddLevelCommand(std::unique_ptr<Level<T>> level) const noexcept {
				return std::make_unique<AddLevelCommand<T>>(std::move(level));
			}

			template<typename T, typename... Func>
			[[nodiscard]] std::unique_ptr<IRequestCommand>
				CreateAddLevelCommand(std::unique_ptr<Level<T>> level, Func&&... customFunc) const noexcept {
				return std::make_unique<AddLevelCommand<T>>(std::move(level), std::forward<Func>(customFunc)...);
			}

			[[nodiscard]] std::unique_ptr<IRequestCommand> CreateLoadLevelCommand(const std::string& name) const noexcept {
				return std::make_unique<LoadLevelCommand>(name);
			}

			[[nodiscard]] std::unique_ptr<IRequestCommand> CreateCleanLevelCommand(const std::string& name) const noexcept {
				return std::make_unique<CleanLevelCommand>(name);
			}

			template<template<typename> class System>
			[[nodiscard]] std::unique_ptr<IRequestCommand> CreateAddGlobalSystemCommand() const noexcept {
				return std::make_unique<AddGlobalSystemCommand<System>>();
			}

		private:
			// すべてのコマンドを実行する関数
			// WorldでLevelを更新する前に呼び出す
			void FlashAllCommand(World<LevelTypes...>* pWorld) {

				decltype(requests) localRequests;
				{
					std::lock_guard<std::mutex> lock(*requestMutex);
					std::swap(localRequests, requests);

					//念のためクリア
					requests.clear();
				}

				if (localRequests.empty()) return;

				auto session = pWorld->GetSession();

				for (auto& cmd : localRequests) {
					cmd->Execute(&session);
				}
			}
		private:
			//ムーブ可能にするためヒープで保持
			std::shared_ptr<std::mutex> requestMutex;
			std::vector<std::unique_ptr<IRequestCommand>> requests;
		public:
			STATIC_SERVICE_TAG
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
			serviceLocator(std::move(other.serviceLocator)),
			requestService(std::move(other.requestService)) {
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
				requestService = std::move(other.requestService);
			}
			return *this;
		}

		/**
		 * @brief ワールドリクエストサービスをサービスロケーターに登録する関数
		 * @details GameEngineの初期化時に呼び出す
		 */
		void RegisterRequestService() {
			// ワールドリクエストサービスをサービスロケーターに登録
			ECS::ServiceLocator::WorldAccessor::AddStaticService(&this->serviceLocator, &requestService);
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
					(..., [](auto& vecs, auto& mainFunc, auto& subFunc) {
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

#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVEL, /*leaf=*/false, "GlobalSystem" });
			}

			globalSystem.ShowDebugSystemTree();
#endif

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
					&latch]()
					{
						task(serviceLocator, deltaTime, executor);
						latch.CountDown();
					}
				);
			}

			//グローバルシステムの更新処理実行
			globalSystem.UpdateGlobal(serviceLocator, executor);

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
		/**
		 * @brief ワールドへの要求を管理するサービスの取得
		 * @return RequestService& ワールドへの要求を管理するサービスへの参照
		 */
		[[nodiscard]] RequestService& GetRequestServiceNoLock() {
			return requestService;
		}

		void LoadLevel(const std::string levelName)
		{
			auto requestCmd = requestService.CreateLoadLevelCommand(levelName);
			requestService.PushCommand(std::move(requestCmd));
		}
	private:
		/*
		* @brief Worldへのセッション(レベルの追加など)を取得する関数
		* @return Session Worldのセッションオブジェクト
		*/
		[[nodiscard]] Session GetSession() {
			return Session(*this);
		}
	private:
		std::tuple<std::vector<LevelHolder<LevelTypes>>...> levelSets;
		ECS::ServiceLocator serviceLocator;
		RequestService requestService;

		struct NonePartition {
		};

		//Partitionクラスを使用しないので適当な型を入れる
		ECS::SystemScheduler<NonePartition> globalSystem;
	};
}