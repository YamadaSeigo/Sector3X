/*****************************************************************//**
 * @file   ServiceLocator.h
 * @brief サービスロケータークラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <execution>

#include "../../Graphics/RenderService.h"
#include "../../Util/TypeChecker.hpp"
 //#include "Input/InputService.h"

#include "../ThreadPoolExecutor.h"
#include "../../Debug/assert_config.h"

namespace SFW
{
	// 前方宣言
	template<typename... LevelTypes>
	class World;

	namespace ECS
	{
		/**
		 * @brief Systemが依存するサービスを管理するクラス
		 * @details 内部でshared_mutexを使用しているのでムーブのみ許可する
		 */
		class ServiceLocator {
			struct Location {
				void* servicePtr = nullptr;
				size_t updateIndex = 0;
				bool isStatic = false;

				Location() = default;
				explicit Location(void* ptr, size_t index = 0, bool isStaticService = false) noexcept
					: servicePtr(ptr), updateIndex(index), isStatic(isStaticService) {
				}
			};

			inline static bool created = false; // クラススコープに移動
		public:
			/**
			 * @brief コンストラクタ
			 * @param executor 更新処理の際の並列ために使用するスレッドプール
			 * @details 複数回生成すると実行時エラー
			 */
			template<PointerType... Service>
			explicit ServiceLocator(Service... service)
				: mapMutex(std::make_unique<std::shared_mutex>()) {
				SFW_ASSERT(!created && "ServiceLocator instance already created.");
				created = true;

				plan_ = std::make_shared<ExecPlan>();

				(AllRegisterStaticServiceWithArg<std::remove_pointer_t<Service>>(service), ...);

				RebuildPlan_NeedLock();
			}

			/**
			 * @brief　コピーコンストラクタを削除
			 */
			ServiceLocator(const ServiceLocator&) = delete;
			/**
			 * @brief コピー代入演算子を削除
			 */
			ServiceLocator& operator=(const ServiceLocator&) = delete;
			/**
			 * @brief ムーブコンストラクタ
			 */
			ServiceLocator(ServiceLocator&&) noexcept = default;
			/**
			 * @brief ムーブ代入演算子
			 */
			ServiceLocator& operator=(ServiceLocator&&) noexcept = default;
			/**
			 * @brief デストラクタ
			 */
			~ServiceLocator() {
				created = false;
			}

			/**
			 * @brief 初期化処理(複数回呼び出し禁止)
			 */
			template<typename... Services>
			void InitAndRegisterStaticService() noexcept {

				std::unique_lock<std::shared_mutex> lock(*mapMutex);
				(AllRegisterStaticService<Services>(), ...);

				RebuildPlan_NeedLock();
			}
			/**
			 * @brief 動的サービスの登録を行う
			 * @tparam T サービスの型
			 */
			template<typename... T>
			void RegisterDynamicService() noexcept {
				// 動的サービスの登録を行う
				(AllRegisterDynamicService<T>(), ...);
			}
			/**
			 * @brief サービスの登録を解除する(動的なサービス限定)
			 * @tparam T サービスの型
			 */
			template<typename T>
			void UnregisterDynamicService() {
				static_assert(!T::isStatic, "Cannot unregister static service.");

				std::unique_lock<std::shared_mutex> lock(*mapMutex);
				auto iter = services.find(typeid(T));

				if (iter != services.end())
				{
					if constexpr (isUpdateService<T>) {
						auto index = iter->second.updateIndex;
						if (updateServices.empty() || index >= updateServices.size()) {
							SFW_ASSERT(false && "Invalid update service index.");
							return;
						}

						auto updateService = updateServices[index];
						auto it = services.find(updateServices.back()->typeIndex);
						if (it != services.end()) {
							it->second.updateIndex = index;
							std::swap(updateServices[index], updateServices.back());
							updateServices.pop_back();
						}
						else {
							SFW_ASSERT(false && "Update service not found in the update services list.");
						}
					}
					if constexpr (std::is_base_of_v<ICommitService, T>) {
						auto commitService = static_cast<ICommitService*>(iter->second.servicePtr);
						auto it = std::find(commitServices.begin(), commitServices.end(), commitService);
						if (it != commitServices.end()) {
							commitServices.erase(it);
						}
						else {
							SFW_ASSERT(false && "Commit service not found in the commit services list.");
						}
					}
					services.erase(iter);
				}

				RebuildPlan_NeedLock();
			}
			/**
			 * @brief サービスを取得する(const強制解除)
			 * @tparam T サービスの型
			 * @return サービスのポインタ
			 */
			template<typename T>
			T* Get() const noexcept {
				//static_assert(T::isStatic || true, "Dynamic services can be null");

				std::shared_lock<std::shared_mutex> lock(*mapMutex);
				auto it = services.find(typeid(T));
				if (it == services.end()) [[unlikely]] {
					SFW_ASSERT(!T::isStatic && "Static service not registered!");
					return nullptr;
				}
				return static_cast<T*>(it->second.servicePtr);
			}

			/**
			 * @brief サービスの更新を行う
			 */
			void UpdateService(double dt, IThreadExecutor* executor) {
				// ロック不要：不変 plan_ を読むだけ
				auto p = plan_;                 // shared_ptr のコピーは lock-free
				if (!p) {
					// 初回だけ保険（通常は初期化で構築済み）
					std::shared_lock<std::shared_mutex> lk(*mapMutex);
					// Executor 取得や束ね直しは行わず、直列フォールバックでも良いが
					// プラン未構築なら何もしないか、旧直列ループに退避してもOK
					for (auto* s : updateServices) if (s) s->PreUpdate(dt); // フォールバック
					return;
				}
				for (const auto& phase : plan_->phases) {

					// group>=1のグループを並列で更新
					ThreadCountDownLatch latch((int)phase.parallelGroups.size());
					for (auto& g : phase.parallelGroups)
					{
						executor->Submit([&g, &latch, dt]() {
							// グループ内は order 順に直列
							for (IUpdateService* s : g.serial) {
								s->PreUpdate(dt);
							}
							latch.CountDown();
							});
					}

					// group==0 の“直列レーン”をメインスレッドで順に実行
					for (auto* s : phase.serialLane) s->PreUpdate(dt);

					//並列更新待ち
					latch.Wait();
				}
			}

			/**
			 * @brief コミットサービスの関数呼び出し
			 */
			void CommitService(double deltaTime) {
				for (auto* service : commitServices) {
					service->Commit(deltaTime);
				}
			}

		private:
			/**
			 * @brief サービスを登録する(静的なサービス限定、引数あり)
			 * @param service サービスのポインタ
			 */
			template<typename T>
			void AllRegisterStaticServiceWithArg(T* service) noexcept {
				static_assert(T::isStatic, "Cannot register dynamic service with argument.");
				SFW_ASSERT((!IsRegistered<T>() && service) && "Cannot register service.");

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service);
					updateService->typeIndex = typeid(T);
					updateService->phase = T::updatePhase;
					updateService->group = T::updateGroup;
					updateService->order = T::updateOrder;
					updateServices.push_back(updateService);
				}
				if constexpr (std::is_base_of_v<ICommitService, T>) {
					ICommitService* commitService = static_cast<ICommitService*>(service);
					commitServices.push_back(commitService);
				}

				services[typeid(T)] = Location{ service,updateIndex, T::isStatic };
			}

			/**
			 * @brief すべてのサービスを登録する
			 * @param service サービスのポインタ
			 */
			template<typename T>
			void AllRegisterStaticService() noexcept {
				static_assert(T::isStatic, "Cannot register dynamic service.");
				SFW_ASSERT((!IsRegistered<T>()) && "Cannot re-register static service.");

				// ServiceLocator は唯一のインスタンスとして設計されており、
				// 各サービス型ごとに1つだけ static に保持して問題ない
				static std::unique_ptr<T> service = std::make_unique<T>();

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service.get());
					updateService->typeIndex = typeid(T);
					updateService->phase = T::updatePhase;
					updateService->group = T::updateGroup;
					updateService->order = T::updateOrder;
					updateServices.push_back(updateService);
				}
				if constexpr (std::is_base_of_v<ICommitService, T>) {
					ICommitService* commitService = static_cast<ICommitService*>(service.get());
					commitServices.push_back(commitService);
				}

				services[typeid(T)] = Location{ service.get(),updateIndex, T::isStatic };
			}
			/**
			 * @brief サービスを登録する(動的なサービス限定)
			 * @tparam T サービスの型
			 */
			template<typename T>
			void AllRegisterDynamicService() noexcept {
				static_assert(!T::isStatic, "Cannot re-register static service.");

				if (IsRegistered<T>()) {
					SFW_ASSERT(false && "Service already registered.");
					return;
				}

				// ServiceLocator は唯一のインスタンスとして設計されており、
				// 各サービス型ごとに1つだけ static に保持して問題ない
				static std::unique_ptr<T> service;
				service = std::make_unique<T>();

				std::unique_lock<std::shared_mutex> lock(*mapMutex);

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service.get());
					updateService->typeIndex = typeid(T);
					updateService->phase = T::updatePhase;
					updateService->group = T::updateGroup;
					updateService->order = T::updateOrder;
					updateServices.push_back(updateService);
				}
				if constexpr (std::is_base_of_v<ICommitService, T>) {
					ICommitService* commitService = static_cast<ICommitService*>(service.get());
					commitServices.push_back(commitService);
				}

				services[typeid(T)] = Location{ service.get(),updateIndex,T::isStatic };

				RebuildPlan_NeedLock();
			}

			/**
			 * @brief サービスが登録されているかどうか
			 * @return 登録されている場合true
			 */
			template<typename T>
			bool IsRegistered() const {
				return services.find(typeid(T)) != services.end();
			}
			/**
			 * @brief UpdateServiceから実行プランを再構築する
			 */
			void RebuildPlan_NeedLock() {

				// 登録済み UpdateEntry をフェーズ→グループ→order で束ね直す
				// ここは、前回案の通り UpdateEntry ベクタを持っている前提
				std::vector<UpdateEntry> entries;
				entries.reserve(updateServices.size());
				for (auto* s : updateServices) {
					if (!s) continue;
					UpdateEntry e{};
					e.ptr = s;
					e.typeIndex = s->typeIndex;              // 既存コードが設定済み :contentReference[oaicite:4]{index=4}
					e.phase = s->phase;
					e.group = s->group;
					e.order = s->order;
					entries.push_back(e);
				}
				uint16_t minPhase = UINT16_MAX, maxPhase = 0;
				for (auto& e : entries) { minPhase = (std::min)(minPhase, e.phase); maxPhase = (std::max)(maxPhase, e.phase); }
				if (entries.empty()) { return; }

				plan_->phases.resize(size_t(maxPhase - minPhase + 1));
				// group id は密でない可能性あり → まず grouping
				std::unordered_map<uint16_t, std::vector<UpdateEntry>> tmpGroups;

				for (uint16_t ph = minPhase; ph <= maxPhase; ++ph) {
					// フェーズ内：group==0 は“直列レーン”、group>=1 は“並列グループ”
					std::unordered_map<uint16_t, std::vector<UpdateEntry>> groups;
					std::vector<UpdateEntry> lane0;
					for (auto& e : entries) if (e.phase == ph) {
						if (e.group == 0) lane0.push_back(e);
						else groups[e.group].push_back(e);
					}

					auto& phase = plan_->phases[size_t(ph - minPhase)];

					// lane0 は order 昇順で直列実行
					std::sort(lane0.begin(), lane0.end(), [](auto& a, auto& b) { return a.order < b.order; });
					phase.serialLane.clear();
					phase.serialLane.reserve(lane0.size());
					for (auto& e : lane0) phase.serialLane.push_back(e.ptr);

					// group>=1 は各グループ内を order ソート → parallelGroups に入れる
					phase.parallelGroups.clear();
					phase.parallelGroups.reserve(groups.size());
					for (auto& kv : groups) {
						auto& vec = kv.second;
						std::sort(vec.begin(), vec.end(), [](auto& a, auto& b) { return a.order < b.order; });
						ExecPlan::GroupPlan gp;
						gp.serial.reserve(vec.size());
						for (auto& e : vec) gp.serial.push_back(e.ptr);
						phase.parallelGroups.push_back(std::move(gp));
					}
				}
			}

			/**
			 * @brief サービスマップ(Get関数で対象の型にcastする)
			 */
			std::unordered_map<std::type_index, Location> services;

			//サービスマップへのアクセス同期
			std::unique_ptr<std::shared_mutex> mapMutex;

			struct UpdateEntry {
				IUpdateService* ptr{};
				std::type_index typeIndex = typeid(void);
				uint16_t phase = 0;
				uint16_t group = 0;
				uint16_t order = 0;
			};

			// 実行プラン（不変オブジェクトとして共有）
			struct ExecPlan {
				struct GroupPlan { std::vector<IUpdateService*> serial; };      // グループ内は直列
				struct PhasePlan {
					std::vector<IUpdateService*> serialLane; // group==0 を直列で実行
					std::vector<GroupPlan> parallelGroups;  // group>=1 を並列グループとして実行
				};
				std::vector<PhasePlan> phases;
			};
			// 更新サービスのリスト
			std::shared_ptr<ExecPlan> plan_;    // 毎フレーム読み取りのみ

			std::vector<IUpdateService*> updateServices;

			std::vector<ICommitService*> commitServices;

		public:
			// ワールドにだけ静的サービスを追加登録させるためのフレンドクラス
			// WorldRequestServiceを登録するために利用している
			class WorldAccessor {
				template<PointerType... Services>
				static void AddStaticService(ServiceLocator* locator, Services... service) {
					std::unique_lock<std::shared_mutex> lock(*locator->mapMutex);
					(locator->AllRegisterStaticServiceWithArg<std::remove_pointer_t<Services>>(service), ...);

					locator->RebuildPlan_NeedLock();
				}

				template<typename... LevelTypes>
				friend class ::SFW::World;
			};
		};
	}
}
