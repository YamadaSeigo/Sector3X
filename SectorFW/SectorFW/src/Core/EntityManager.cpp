#include "Core/ECS/EntityManager.h"
#include "message.h"

namespace SectorFW
{
	namespace ECS
	{
		void EntityManager::DestroyEntity(EntityID id)
		{
			if (locations.contains(id)) {
				EntityLocation loc = locations[id];
				loc.chunk->RemoveEntitySwapPop(loc.index);
				locations.erase(id);
			}
			for (auto& [_, store] : sparseStores) {
				store->Remove(id);
			}

			entityAllocator.Destroy(id);
		}

		ComponentMask EntityManager::GetMask(EntityID id) const noexcept
		{
			auto iter = locations.find(id);
			if (iter != locations.end())
			{
				return iter->second.chunk->GetComponentMask();
			}

			ComponentMask componentMask;
			for (auto& [mask, arch] : archetypeManager.GetAll()) {
				for (auto& chunk : arch->GetChunks()) {
					auto entityCount = chunk->GetEntityCount();
					auto& entities = ArchetypeChunk::EntityAccess::GetEntities(chunk.get());
					for (size_t i = 0; i < entityCount; ++i) {
						if (entities[i] == id)
							componentMask = mask;
					}
				}
			}

			return componentMask;
		}
	}
}