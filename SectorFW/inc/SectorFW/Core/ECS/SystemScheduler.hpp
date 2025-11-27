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
		 * @brief システムが特定のパーティションから派生しているかどうかを確認するコンセプト
		 */
		template<typename T, typename...Deps>
		concept SystemDerived = std::is_base_of_v<ITypeSystem<Deps...>, T>;
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

				typeSys->Start(serviceLocator); // 空のパーティションでStartを呼ぶ

				std::scoped_lock lock(pendingMutex);
				pendingSystems.emplace_back(typeSys);
			}
			/**
			 * @brief すべてのシステムを更新する関数
			 * @param partition 対象のパーティション
			 */
			void UpdateAll(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator, IThreadExecutor* executor) {
				// --- pending の取り込み（ロック最小化） ---
				std::vector<std::unique_ptr<ISystem<Partition>>> newly; // ローカル退避
				//newly.reserve(16);

				if (!pendingSystems.empty()) {
					std::scoped_lock lk(pendingMutex);
					// 一旦 swap で持ち出し → ロック解放後に反映
					newly.swap(pendingSystems);
				}
				if (!newly.empty()) {
					// まとめて systems と accessList に移動/push（reserve で再配置削減）
					updateSystems.reserve(updateSystems.size() + newly.size());
					accessList.reserve(accessList.size() + newly.size());

					for (auto& uptr : newly) {
						// ここで必要ならコンテキスト注入（AddSystem時に済なら不要）
						// uptr->SetContext(serviceLocator);

						// UpdateImpl を持たないシステムは登録しない
						if constexpr (std::remove_reference_t<decltype(*uptr)>::IsUpdateable())
						{
							scheduleDirty = true; // 追加があれば再構築フラグ

							updateSystems.emplace_back(std::move(uptr));

							// AccessInfo を取得してキャッシュ
							// ※ 実装に合わせてメソッド名を調整してください
							accessList.emplace_back(updateSystems.back()->GetAccessInfo());
						}
						else
						{
							systems.emplace_back(std::move(uptr)); // Update不要なシステムは別途保存
						}
					}
				}

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
					ThreadCountDownLatchExternalSync latch(batchMutex, batchCv, (int)group.parallel.size());

					std::vector<uint32_t> serialIndices;

					// par_unseq: 並列+ベクタライズ許可（MSVCの実装でPPL/並列アルゴ適用）
					for (auto idx : group.parallel)
					{
						//idxはコピーキャプチャじゃないと破棄される
						executor->Submit([&, idx]() noexcept {
							// 可能なら no-throw Update を用意、あるいはここでtry/catch
							updateSystems[idx]->Update(partition, levelCtx, serviceLocator, executor);
							latch.CountDown();
							});
					}

					// 直列実行部分
					for (auto idx : group.serial)
					{
						updateSystems[idx]->Update(partition, levelCtx, serviceLocator, executor);
					}

					latch.Wait();
				}
			}

			void CleanSystem(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator) {
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
			}
		private:
			std::vector<std::unique_ptr<ISystem<Partition>>> systems;
			//更新するシステムのリスト
			std::vector<std::unique_ptr<ISystem<Partition>>> updateSystems;
			//アクセス情報のリスト
			std::vector<AccessInfo> accessList;
			//保留中のシステムのリスト
			std::vector<std::unique_ptr<ISystem<Partition>>> pendingSystems;
			//保留中のシステムを管理するためのミューテックス
			std::mutex pendingMutex;

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
								group.serial.push_back(i);;
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
			}
		};
	}// namespace ECS
}// namespace SectorFW