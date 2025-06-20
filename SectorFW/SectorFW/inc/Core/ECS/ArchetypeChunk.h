#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <cassert>
#include <bit>

#include "entity.h"
#include "ComponentTypeRegistry.h"

#include "Util/alignment.h"

namespace SectorFW
{
	namespace ECS
	{
		constexpr size_t ChunkSizeBytes = 16 * 1024;

		class ComponentTypeRegistry;

		//----------------------------------------------
		// Chunk
		//----------------------------------------------
		class ArchetypeChunk {
		public:
			void InitializeLayoutFromMask(const ComponentMask& mask);

			void RegisterLayout(ComponentTypeID id, size_t stride, size_t offset) {
				layout[id] = { offset, stride };
			}

			template<typename T>
			T* GetColumn() {
				ComponentTypeID id = ComponentTypeRegistry::GetID<T>();
				auto& info = layout.at(id);
				return reinterpret_cast<T*>(&buffer[info.offset]);
			}

			size_t AddEntity(EntityID id);

			void RemoveEntitySwapPop(size_t index);

			size_t GetEntityCount() const noexcept { return entityCount; }

			size_t GetCapacity() const noexcept { return capacity; }

			ComponentMask GetComponentMask() const noexcept { return componentMask; }
		private:
			uint8_t buffer[ChunkSizeBytes] = {};

			size_t entityCount = 0;
			size_t capacity = 0;
			std::vector<EntityID> entities;

			ComponentMask componentMask;

			struct ComponentInfo {
				size_t offset;
				size_t stride;
			};

			std::unordered_map<ComponentTypeID, ComponentInfo> layout;

		public:
			struct LayoutAccess
			{
				static std::unordered_map<ComponentTypeID, ComponentInfo>& GetLayout(ArchetypeChunk* chunk) noexcept { return chunk->layout; }

				friend class EntityManager;
			};

			struct EntityAccess
			{
				static std::vector<EntityID>& GetEntities(ArchetypeChunk* chunk) noexcept { return chunk->entities; }

				friend class EntityManager;
			};
		};
	}
}
