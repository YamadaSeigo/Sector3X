/*****************************************************************//**
 * @file   SystemScheduler.h
 * @brief システムスケジューラを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <mutex>
#include <typeindex>
#include <future>
#include <execution>

#include "EntityManager.h"
#include "ITypeSystem.hpp"

#include "../../Debug/ImGuiLayer.h"

#ifdef _ENABLE_IMGUI
#include "../../Debug/UIBus.h"
#endif

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief システムを管理し、競合がないようにスケジューリングするクラス
		 * @tparam Partition パーティションの型
		 */
		template<typename Partition>
		class SystemScheduler {
		public:
			/**
			 * @brief システムを追加する関数
			 * @param serviceLocator サービスロケーター
			 */
			template<template<typename> class SystemType>
			void AddSystem(const ServiceLocator& serviceLocator) {
				auto typeSys = new SystemType<Partition>();
				typeSys->SetContext(serviceLocator);

				std::scoped_lock lock(pendingMutex);
				pendingSystems.emplace_back(typeSys);
			}

			/**
			 * @brief システムを削除する関数
			 */
			template<template<typename> class SystemType>
			void RemoveSystem()
			{
				std::scoped_lock lock(pendingMutex);
				pendingRemoveTypes.emplace_back(typeid(SystemType<Partition>));
			}

			template<template<typename> class SystemType>
			void PauseSystem()
			{
				std::scoped_lock lock(pendingMutex);
				pendingPauseTypes.emplace_back(typeid(SystemType<Partition>));
			}

			template<template<typename> class SystemType>
			void ResumeSystem()
			{
				std::scoped_lock lock(pendingMutex);
				pendingResumeTypes.emplace_back(typeid(SystemType<Partition>));
			}

			void ApplyPendingProcesses(const ServiceLocator& serviceLocator, Partition* partition, LevelContext<Partition>* levelCtx) {
				ApplyPendingAdditions(serviceLocator);
				ApplyPendingRemovals<true>(partition, levelCtx, &serviceLocator);
				ApplyPendingPauseResume();

				if (scheduleDirty) {
					RebuildBatches();
				}
			}

			/**
			 * @brief すべてのシステムを更新する関数.
			 * @param partition 対象のパーティション
			 * @param levelCtx レベルコンテキスト
			 * @param serviceLocator サービスロケーター
			 * @param executor スレッドエグゼキューター
			 * @details 保留された追加・削除・一時停止・再開の処理を適用した後、スケジュールが変更された場合はバッチを再構築し、各バッチごとに並列実行と直列実行を行う。例外は各システム内で握り潰さず、ここで個別捕捉することも可能。
			 */
			void UpdateAllSystem(Partition& partition, LevelContext<Partition>& levelCtx, const ServiceLocator& serviceLocator, IThreadExecutor* executor) {
				//新しいシステムの取り込み
				ApplyPendingAdditions(serviceLocator);
				//削除するシステムの適用
				ApplyPendingRemovals<true>(&partition, &levelCtx, &serviceLocator);
				// 一時停止・再開の適用
				ApplyPendingPauseResume();

				// --- 並列実行プランの再構築（必要時のみ） ---
				if (scheduleDirty) {
					RebuildBatches();
				}

#ifdef _ENABLE_IMGUI
				size_t n = updateSystems.size();
				for (size_t i = 0; i < n; ++i)
				{
					auto g = Debug::BeginTreeWrite(); // lock & back buffer
					auto& frame = g.data();

					// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
					std::string systemName = updateSystems[i]->derived_name();
					std::string partitionName = typeid(Partition).name();
					frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_SYSTEM, /*leaf=*/true, std::string(systemName.begin() + 6, systemName.end() - (partitionName.size() + 2)) });
				} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif

				// --- バッチごとに並列実行 ---
				// 例外は各システム内で握り潰さず、ここで個別捕捉するのも可
				for (const auto& group : batches) {
					// 有効な parallel 数だけカウント
					int parallelEnabledCount = 0;
					for (auto idx : group.parallel)
					{
						if (idx < updateEnabled.size() && updateEnabled[idx]) ++parallelEnabledCount;
					}

					ThreadCountDownLatchExternalSync latch(batchMutex, batchCv, parallelEnabledCount);

					// parallel
					for (auto idx : group.parallel)
					{
						if (idx >= updateEnabled.size() || !updateEnabled[idx]) continue;

						executor->Submit([&, idx]() noexcept {
							// 実際にシステムの更新関数を呼び出す inc/core/ecs/ISystem.hpp の Update仮装関数
							updateSystems[idx]->Update(partition, levelCtx, serviceLocator, executor);
							latch.CountDown();
							});
					}

					// serial
					for (auto idx : group.serial)
					{
						if (idx >= updateEnabled.size() || !updateEnabled[idx]) continue;
						updateSystems[idx]->Update(partition, levelCtx, serviceLocator, executor);
					}

					latch.Wait();
				}
			}

			/**
			 * @brief すべてのシステムをグローバルに更新する関数
			 * @param serviceLocator サービスロケーター
			 * @param executor スレッド実行クラス
			 * @details 保留された追加・削除・一時停止・再開の処理を適用した後、スケジュールが変更された場合はバッチを再構築し、各バッチごとに並列実行と直列実行を行う。例外は各システム内で握り潰さず、ここで個別捕捉することも可能。
			　* @note Partition や LevelContext を必要としないシステムの更新に使用する。これらのシステムは UpdateGlobal をオーバーライドしている必要がある。
			 */
			void UpdateGlobalSystem(const ServiceLocator& serviceLocator, IThreadExecutor* executor) {
				//新しいシステムの取り込み
				ApplyPendingAdditions(serviceLocator);
				//削除するシステムの適用
				ApplyPendingRemovals<false>(nullptr, nullptr, nullptr);
				// 一時停止・再開の適用
				ApplyPendingPauseResume();

				// --- 並列実行プランの再構築（必要時のみ） ---
				if (scheduleDirty) {
					RebuildBatches();
				}

				// --- バッチごとに並列実行 ---
				// 例外は各システム内で握り潰さず、ここで個別捕捉するのも可
				for (const auto& group : batches) {
					// 有効な parallel 数だけカウント
					int parallelEnabledCount = 0;
					for (auto idx : group.parallel)
					{
						if (idx < updateEnabled.size() && updateEnabled[idx]) ++parallelEnabledCount;
					}

					ThreadCountDownLatchExternalSync latch(batchMutex, batchCv, parallelEnabledCount);

					// parallel
					for (auto idx : group.parallel)
					{
						if (idx >= updateEnabled.size() || !updateEnabled[idx]) continue;

						executor->Submit([&, idx]() noexcept {
							updateSystems[idx]->Update(serviceLocator, executor);
							latch.CountDown();
							});
					}

					// serial
					for (auto idx : group.serial)
					{
						if (idx >= updateEnabled.size() || !updateEnabled[idx]) continue;
						updateSystems[idx]->Update(serviceLocator, executor);
					}

					latch.Wait();
				}
			}

			void CleanSystem(Partition& partition, LevelContext<Partition>& levelCtx, const ServiceLocator& serviceLocator) {
				for (auto& sys : systems)
				{
					if constexpr (std::remove_reference_t<decltype(*sys)>::IsEndSystem())
					{
						sys->End(partition, levelCtx, serviceLocator);
					}
				}
				for (auto& sys : updateSystems)
				{
					if constexpr (std::remove_reference_t<decltype(*sys)>::IsEndSystem())
					{
						sys->End(partition, levelCtx, serviceLocator);
					}
				}

				{
					std::unique_lock lockPending(pendingMutex);
					systems.clear();
					updateSystems.clear();
					accessList.clear();
					pendingSystems.clear();
					pendingRemoveTypes.clear();
					pendingPauseTypes.clear();
					pendingResumeTypes.clear();
					updateEnabled.clear();
				}
				{
					std::unique_lock lockBatck(batchMutex);
					batches.clear();
				}
			}

			void ShowDebugSystemTree(uint32_t treeDepth) {
#ifdef _ENABLE_IMGUI
				size_t n = updateSystems.size();
				for (size_t i = 0; i < n; ++i)
				{
					auto g = Debug::BeginTreeWrite(); // lock & back buffer
					auto& frame = g.data();

					// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
					std::string systemName = updateSystems[i]->derived_name();
					std::string partitionName = typeid(Partition).name();
					frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/(Debug::WorldTreeDepth)treeDepth, /*leaf=*/true, std::string(systemName.begin() + 6, systemName.end() - (partitionName.size() + 2)) });
				} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			}

		private:
			std::vector<std::unique_ptr<ISystem<Partition>>> systems;
			//更新するシステムのリスト
			std::vector<std::unique_ptr<ISystem<Partition>>> updateSystems;
			//アクセス情報のリスト
			std::vector<AccessInfo> accessList;
			//保留中のシステムのリスト
			std::vector<std::unique_ptr<ISystem<Partition>>> pendingSystems;
			//保留中の削除タイプのリスト
			std::vector<std::type_index> pendingRemoveTypes;
			//保留中のシステムを管理するためのミューテックス
			std::mutex pendingMutex;

			// --- Pause/Resume 状態 ---
			// updateSystems と同じインデックスで管理する（batches が index を持つため）
			std::vector<uint8_t> updateEnabled; // 1=enabled, 0=paused

			// pending（Add/Remove と同じ思想）
			std::vector<std::type_index> pendingPauseTypes;
			std::vector<std::type_index> pendingResumeTypes;

			struct Group {
				std::vector<uint32_t> serial;
				std::vector<uint32_t> parallel;
			};

			//競合のない並列実行グループ（インデックス集合）
			std::vector<Group> batches;

			//並列処理の同期用
			std::mutex batchMutex;
			std::condition_variable batchCv;

			//追加入替時のみ再構築
			bool scheduleDirty = true;
			/**
			 * @brief アクセス情報が競合しているかどうかを確認する関数
			 * @param a アクセス情報A
			 * @param b アクセス情報B
			 * @return bool 競合している場合はtrue、そうでない場合はfalse
			 */
			static inline bool Conflicts(const AccessInfo& a, const AccessInfo& b) noexcept {
				for (ComponentTypeID id : a.write) {
					if (b.read.count(id) || b.write.count(id)) return true;
				}
				for (ComponentTypeID id : a.read) {
					if (b.write.count(id)) return true;
				}
				return false;
			}
			/**
			 * @brief バッチを再構築（競合しないグループに分割）
			 */
			void RebuildBatches() {
				batches.clear();
				batches.reserve(updateSystems.size() / 2 + 1);

				// Greedy coloring 的に最初に入れられるバッチへ突っ込む
				for (uint32_t i = 0; i < updateSystems.size(); ++i) {
					const auto& ai = accessList[i];
					bool placed = false;
					for (auto& group : batches) {
						bool ok = true;
						// そのバッチ内と競合しないか確認（早期break）
						for (size_t j : group.serial) {
							const auto& aj = accessList[j];
							//念のため二重チェック
							if (Conflicts(ai, aj) || Conflicts(aj, ai)) {
								ok = false; break;
							}
						}
						if (!ok) continue;

						for (size_t j : group.parallel) {
							const auto& aj = accessList[j];
							if (Conflicts(ai, aj) || Conflicts(aj, ai)) {
								ok = false; break;
							}
						}
						if (ok) {
							bool isParallel = updateSystems[i]->IsParallelUpdate();
							if (isParallel)
								group.parallel.push_back(i);
							else
								group.serial.push_back(i);
							placed = true;
							break;
						}
					}
					if (!placed) {
						batches.emplace_back();
						bool isParallel = updateSystems[i]->IsParallelUpdate();
						if (isParallel)
							batches.back().parallel.push_back(i);
						else
							batches.back().serial.push_back(i);
					}
				}
				scheduleDirty = false;

				// debug: updateSystems と updateEnabled のサイズが一致しているべき
				assert(updateEnabled.size() == updateSystems.size());
			}

			void ApplyPendingAdditions(const ServiceLocator& serviceLocator)
			{
				std::vector<std::unique_ptr<ISystem<Partition>>> newly;
				{
					std::scoped_lock lk(pendingMutex);
					if (!pendingSystems.empty())
						newly.swap(pendingSystems);
				}
				if (newly.empty()) return;
				// まとめて systems と accessList に移動/push（reserve で再配置削減）
				updateSystems.reserve(updateSystems.size() + newly.size());
				accessList.reserve(accessList.size() + newly.size());
				for (auto& uptr : newly) {
					// 空のパーティションでStartを呼ぶ
					uptr->Start(serviceLocator);

					// UpdateImpl を持たないシステムは登録しない
					if constexpr (std::remove_reference_t<decltype(*uptr)>::IsUpdateable())
					{
						scheduleDirty = true; // 追加があれば再構築フラグ
						updateSystems.emplace_back(std::move(uptr));
						// AccessInfo を取得してキャッシュ
						accessList.emplace_back(updateSystems.back()->GetAccessInfo());
						// 追加された UpdateSystem はデフォルト
						updateEnabled.emplace_back(1);
					}
					else
					{
						systems.emplace_back(std::move(uptr)); // Update不要なシステムは別途保存
					}
				}
			}

			// systemを総検索するので処理は遅め。Removeを頻繁に行う設計の場合は注意。
			template<bool CallEnd = true>
			void ApplyPendingRemovals(Partition* partition, LevelContext<Partition>* levelCtx, const ServiceLocator* serviceLocator)
			{
				std::vector<std::type_index> toRemove;
				{
					std::scoped_lock lk(pendingMutex);
					if (!pendingRemoveTypes.empty())
						toRemove.swap(pendingRemoveTypes);
				}
				if (toRemove.empty()) return;

				auto match = [&](const std::unique_ptr<ISystem<Partition>>& p, const std::type_index& ti)
					{
						return std::type_index(typeid(*p)) == ti;
					};

				// 非Update系 systems
				for (const auto& ti : toRemove)
				{
					for (size_t i = 0; i < systems.size(); )
					{
						if (match(systems[i], ti))
						{
							if constexpr (CallEnd && std::remove_reference_t<decltype(*systems[i])>::IsEndSystem())
								systems[i]->End(*partition, *levelCtx, *serviceLocator);

							systems.erase(systems.begin() + i);
						}
						else ++i;
					}
				}

				// Update系 updateSystems + accessList
				for (const auto& ti : toRemove)
				{
					for (size_t i = 0; i < updateSystems.size(); )
					{
						if (match(updateSystems[i], ti))
						{
							if constexpr (CallEnd && std::remove_reference_t<decltype(*updateSystems[i])>::IsEndSystem())
								updateSystems[i]->End(*partition, *levelCtx, *serviceLocator);

							updateSystems.erase(updateSystems.begin() + i);
							accessList.erase(accessList.begin() + i);
							updateEnabled.erase(updateEnabled.begin() + i);
							scheduleDirty = true;
						}
						else ++i;
					}
				}
			}

			void ApplyPendingPauseResume() noexcept
			{
				std::vector<std::type_index> toPause;
				std::vector<std::type_index> toResume;

				{
					std::scoped_lock lk(pendingMutex);
					if (!pendingPauseTypes.empty())  toPause.swap(pendingPauseTypes);
					if (!pendingResumeTypes.empty()) toResume.swap(pendingResumeTypes);
				}

				if (!toPause.empty())
				{
					for (const auto& ti : toPause)
					{
						for (size_t i = 0; i < updateSystems.size(); ++i)
						{
							if (std::type_index(typeid(*updateSystems[i])) == ti)
							{
								if (i < updateEnabled.size()) updateEnabled[i] = 0;
							}
						}
					}
				}

				if (!toResume.empty())
				{
					for (const auto& ti : toResume)
					{
						for (size_t i = 0; i < updateSystems.size(); ++i)
						{
							if (std::type_index(typeid(*updateSystems[i])) == ti)
							{
								if (i < updateEnabled.size()) updateEnabled[i] = 1;
							}
						}
					}
				}
			}
		};
	}// namespace ECS
}// namespace SectorFW
