#include "Core/ECS/EntityManager.h"
#include "message.h"

namespace SectorFW
{
	namespace ECS
	{
		void EntityManager::DestroyEntity(EntityID id)
		{
			// 事前にスワップ相手候補を把握（ロック不要：chunk 側は別同期レイヤ）
			if (true) {
				std::unique_lock<std::shared_mutex> wlock(locationsMutex);
				auto it = locations.find(id);
				if (it != locations.end()) {
					EntityLocation loc = it->second;
					ArchetypeChunk* chunk = loc.chunk;
					const size_t idx = loc.index;
					const size_t lastIndexBefore = chunk->GetEntityCount() - 1;
					// 先に Remove してから locations を更新
					chunk->RemoveEntitySwapPop(idx);
					// スワップで詰められた場合、末尾ID → idx に移動するので更新
					if (idx < lastIndexBefore) {
						EntityID swappedId = ArchetypeChunk::Accessor::GetEntities(chunk)[idx]; // Remove 後は idx に来ている
						auto its = locations.find(swappedId);
						if (its != locations.end()) {
							its->second = { chunk, idx };
						}
					}
					locations.erase(it);
				}
			}
			for (auto& [_, store] : sparseStores) {
				store->Remove(id);
			}

			entityAllocator.Destroy(id);
		}

		ComponentMask EntityManager::GetMask(EntityID id) const noexcept
		{
			//idのチャンクが存在する場合はそのマスクを返す
			std::shared_lock<std::shared_mutex> rlock(locationsMutex);
			auto iter = locations.find(id);
			if (iter != locations.end())
			{
				return iter->second.chunk->GetComponentMask();
			}

			//idのチャンクが存在しない場合は、全アーキタイプを検索してマスクを取得する
			ComponentMask componentMask;
			for (auto& [mask, arch] : archetypeManager.GetAll()) {
				for (auto& chunk : arch->GetChunks()) {
					auto entityCount = chunk->GetEntityCount();
					const auto& entities = ArchetypeChunk::Accessor::GetEntities(chunk.get());
					for (size_t i = 0; i < entityCount; ++i) {
						if (entities[i] == id)
							return mask; // マスクが見つかったら返す
					}
				}
			}

			return componentMask;
		}
	}
}