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

#include "EntityManager.h"
#include "ITypeSystem.hpp"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief システムが特定のパーティションから派生しているかどうかを確認するコンセプト
		 */
		template<typename T, typename...Deps>
		concept SystemDerived = std::is_base_of_v<ITypeSystem<Deps...>, T>;
		/**
		 * @brief システムスケジューラを定義するクラス
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

				accessList.push_back(typeSys->GetAccessInfo());
				systems.emplace_back(typeSys);
			}
			/**
			 * @brief システムをキューに追加する関数
			 */
			 // TODO
			template<typename SystemType>
			void QueueSystem() {
				/*auto sys = std::make_unique<SystemType>();

				std::scoped_lock lock(pendingMutex);
				pendingSystems.push_back(std::move(system));*/
			}
			/**
			 * @brief すべてのシステムを更新する関数
			 * @param partition 対象のパーティション
			 */
			void UpdateAll(Partition& partition, const ServiceLocator& serviceLocator) {
				if (!pendingSystems.empty())
				{
					std::scoped_lock lock(pendingMutex);
					systems.reserve(systems.size() + pendingSystems.size());
					for (auto& sys : pendingSystems)
						systems.emplace_back(std::move(sys));
					pendingSystems.clear();
				}

				// ビルド：System依存グラフ
				size_t n = systems.size();
				std::vector<std::vector<size_t>> adjacency(n);
				std::vector<size_t> indegree(n, 0);

				for (size_t i = 0; i < n; ++i) {
					for (size_t j = i + 1; j < n; ++j) {
						if (i == j) continue;
						if (HasConflict(accessList[i], accessList[j])) {
							adjacency[i].push_back(j);
							indegree[j]++;
						}
					}
				}

				// トポロジカルソート + 同時実行グループごとに実行
				std::vector<bool> done(n, false);
				while (true) {
					std::vector<size_t> parallelGroup;
					for (size_t i = 0; i < n; ++i) {
						if (!done[i] && indegree[i] == 0) {
							parallelGroup.push_back(i);
							done[i] = true;
						}
					}
					if (parallelGroup.empty()) break;

					std::vector<std::future<void>> futures;
					for (size_t i : parallelGroup) {
						futures.emplace_back(std::async(std::launch::async, [&partition, this, i, &serviceLocator]()
							{
								systems[i]->Update(partition, serviceLocator);
							}
						));
					}
					for (auto& f : futures) f.get();

					for (size_t i : parallelGroup) {
						for (size_t j : adjacency[i]) {
							indegree[j]--;
						}
					}
				}
			}
		private:
			/**
			 * @brief アクセス情報が競合しているかどうかを確認する関数
			 * @param a アクセス情報A
			 * @param b アクセス情報B
			 * @return bool 競合している場合はtrue、そうでない場合はfalse
			 */
			bool HasConflict(const AccessInfo& a, const AccessInfo& b) noexcept {
				for (ComponentTypeID id : a.write) {
					if (b.read.count(id) || b.write.count(id)) return true;
				}
				for (ComponentTypeID id : a.read) {
					if (b.write.count(id)) return true;
				}
				return false;
			}
			/**
			 * @brief システムのリスト
			 */
			std::vector<std::unique_ptr<ISystem<Partition>>> systems;
			/**
			 * @brief アクセス情報のリスト
			 */
			std::vector<AccessInfo> accessList;
			/**
			 * @brief 保留中のシステムのリスト
			 */
			std::vector<std::unique_ptr<ISystem<Partition>>> pendingSystems;
			/**
			 * @brief 保留中のシステムを管理するためのミューテックス
			 */
			std::mutex pendingMutex;
		};
	}
}