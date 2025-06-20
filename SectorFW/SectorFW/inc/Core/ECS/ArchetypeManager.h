#pragma once

#include "Archetype.h"

namespace SectorFW
{
	namespace ECS
	{
		//----------------------------------------------
		// Archetype Manager: Creates and finds Archetypes by mask
		//----------------------------------------------
		class ArchetypeManager {
		public:
			Archetype* GetOrCreate(const ComponentMask& mask);

			const std::unordered_map<ComponentMask, std::unique_ptr<Archetype>>& GetAll() const noexcept {
				return archetypes;
			}

		private:
			std::unordered_map<ComponentMask, std::unique_ptr<Archetype>> archetypes;
		};

		//----------------------------------------------
		// EntityLocation: Maps EntityID to Chunk and index
		//----------------------------------------------
		struct EntityLocation {
			ArchetypeChunk* chunk;
			size_t index;
		};
	}
}