#include "Core/ECS/ArchetypeManager.h"

namespace SFW
{
	namespace ECS
	{
		Archetype* ArchetypeManager::GetOrCreate(const ComponentMask& mask)
		{
			auto [it, inserted] = archetypeIndices.try_emplace(mask, 0);
			if (inserted) {
				archetypeData.push_back(std::make_unique<Archetype>(mask));
				it->second = (uint32_t)archetypeData.size() - 1;
			}
			return archetypeData[it->second].get();
		}
	}
}