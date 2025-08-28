#include "Core/ECS/ArchetypeManager.h"

namespace SectorFW
{
	namespace ECS
	{
		Archetype* ArchetypeManager::GetOrCreate(const ComponentMask& mask)
		{
			auto [it, inserted] = archetypes.try_emplace(mask, nullptr);
			if (inserted) {
				it->second = std::make_unique<Archetype>(mask);
			}
			return it->second.get();
		}
	}
}