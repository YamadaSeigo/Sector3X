#include "Core/ECS/ArchetypeManager.h"

namespace SectorFW
{
	namespace ECS
	{
		Archetype* ArchetypeManager::GetOrCreate(const ComponentMask& mask)
		{
			auto it = archetypes.find(mask);
			if (it != archetypes.end()) return it->second.get();

			auto arch = std::make_unique<Archetype>(mask);
			Archetype* ptr = arch.get();
			archetypes[mask] = std::move(arch);
			return ptr;
		}
	}
}