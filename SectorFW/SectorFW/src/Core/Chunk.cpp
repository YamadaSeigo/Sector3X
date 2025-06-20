#include "Core/ECS/ArchetypeChunk.h"
#include "message.h"

namespace SectorFW
{
	namespace ECS
	{
		void ArchetypeChunk::InitializeLayoutFromMask(const ComponentMask& mask)
		{
			componentMask = mask;

			struct Entry {
				ComponentTypeID id;
				size_t size;
				size_t align;
			};
			std::vector<Entry> components;

			// 最小ループでビット走査
			ComponentMask working = mask;
			while (working.any()) {
				ComponentTypeID index = static_cast<ComponentTypeID>(std::countr_zero(working.to_ullong()));
				working.reset(index);
				ComponentMeta meta = ComponentTypeRegistry::GetMeta(index);
				if (meta.isSparse) continue;
				components.push_back({ index, meta.size, meta.align });
			}

			size_t totalSize = 0;
			for (auto& c : components) {
				totalSize = AlignTo(totalSize, c.align);
				totalSize += c.size;
			}

			capacity = totalSize > 0 ? ChunkSizeBytes / totalSize : 0;

			while (true) {
				size_t offset = 0;
				bool fits = true;
				for (auto& c : components) {
					offset = AlignTo(offset, c.align);
					if (offset + c.size * capacity > ChunkSizeBytes) {
						fits = false;
						break;
					}
					layout[c.id] = { offset, c.size };
					offset += c.size * capacity;
				}
				if (fits) break;

				if (capacity == 0) break;

				--capacity;
				layout.clear();
			}

			entities.resize(capacity);
		}

		size_t ArchetypeChunk::AddEntity(EntityID id)
		{
			DYNAMIC_ASSERT_MESSAGE(entityCount < capacity, "entityCount(%d) over capacity(%d)", entityCount, capacity);
			size_t index = entityCount++;
			entities[index] = id;
			return index;
		}

		void ArchetypeChunk::RemoveEntitySwapPop(size_t index)
		{
			if (index < entityCount - 1) {
				entities[index] = entities[entityCount - 1];
				for (auto& [_, info] : layout) {
					std::memcpy(
						&buffer[info.offset + index * info.stride],
						&buffer[info.offset + (entityCount - 1) * info.stride],
						info.stride
					);
				}
			}
			--entityCount;
		}
	}
}