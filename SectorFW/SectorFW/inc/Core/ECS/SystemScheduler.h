#pragma once

#include <mutex>
#include <typeindex>
#include <future>

#include "EntityManager.h"
#include "ITypeSystem.h"

namespace SectorFW
{
	namespace ECS
	{
		template<typename T, typename...Deps>
		concept SystemDerived = std::is_base_of_v<ITypeSystem<Deps...>, T>;

		template<typename Partition>
		class SystemScheduler {
		public:

			template<template<typename> class SystemType>
			void AddSystem(){
				/*std::unique_ptr<ISystem<Partition>> sys = std::make_unique<SystemType<Partition>>();

				accessList.push_back(sys->GetAccessInfo());
				systems.push_back(std::move(sys));*/
			}

			template<template<typename> class SystemType, typename ...Deps>
				requires SystemDerived<SystemType, Deps...>
			void AddSystem(Deps*... deps) {
				/*std::unique_ptr<ISystem<Partition>> sys = std::make_unique<SystemType<Partition>>();

				accessList.push_back(sys->GetAccessInfo());
				sys->SetContext(std::make_tuple(deps...));
				systems.push_back(std::move(sys));*/
			}

			template<typename SystemType>
			void QueueSystem() {
				/*auto sys = std::make_unique<SystemType>();

				std::scoped_lock lock(pendingMutex);
				pendingSystems.push_back(std::move(system));*/
			}


			void UpdateAll(Partition& grid) {
				if (!pendingSystems.empty())
				{
					std::scoped_lock lock(pendingMutex);
					for (auto& sys : pendingSystems)
						systems.push_back(std::move(sys));
					pendingSystems.clear();
				}

				// ビルド：System依存グラフ
				size_t n = systems.size();
				std::vector<std::vector<size_t>> adjacency(n);
				std::vector<size_t> indegree(n, 0);

				for (size_t i = 0; i < n; ++i) {
					for (size_t j = 0; j < n; ++j) {
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
						futures.emplace_back(std::async(std::launch::async, [&grid, this, i]()
							{
								systems[i]->Update(grid);
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
			bool HasConflict(const AccessInfo& a, const AccessInfo& b) {
				for (ComponentTypeID id : a.write) {
					if (b.read.count(id) || b.write.count(id)) return true;
				}
				for (ComponentTypeID id : a.read) {
					if (b.write.count(id)) return true;
				}
				return false;
			}

			std::vector<std::unique_ptr<ISystem<Partition>>> systems;
			std::vector<AccessInfo> accessList;
			std::vector<std::unique_ptr<ISystem<Partition>>> pendingSystems;
			std::mutex pendingMutex;
		};
	}
}