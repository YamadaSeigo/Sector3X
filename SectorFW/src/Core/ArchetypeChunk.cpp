#include "Core/ECS/ArchetypeChunk.h"
#include "message.h"

namespace SectorFW
{
	namespace ECS
	{
		size_t ArchetypeChunk::AddEntity(EntityID id)
		{
			DYNAMIC_ASSERT_MESSAGE(entityCount < layout.capacity, "entityCount(%d) over capacity(%d)", entityCount, layout.capacity);
			size_t index = entityCount++;
			entities[index] = id;
			return index;
		}

		void ArchetypeChunk::RemoveEntitySwapPop(size_t index) noexcept
		{
			if (index < entityCount - 1) {
				entities[index] = entities[entityCount - 1];
				for (auto& infos : layout.info) {
					for (auto& i : infos) {
						std::memcpy(
							&buffer[i.offset + index * i.stride],
							&buffer[i.offset + (entityCount - 1) * i.stride],
							i.stride
						);
					}
				}
			}
			--entityCount;
		}
	}
}