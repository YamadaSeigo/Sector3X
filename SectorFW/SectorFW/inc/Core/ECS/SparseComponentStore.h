#pragma once

#include "Entity.h"

namespace SectorFW
{
	namespace ECS
	{
		template<typename T>
		class SparseComponentStore {
		public:
			void Add(EntityID id, const T& value) { components[id] = value; }
			bool Has(EntityID id) const { return components.find(id) != components.end(); }
			T* Get(EntityID id) const noexcept {
				auto it = components.find(id);
				return it != components.end() ? &it->second : nullptr;
			}
			void Remove(EntityID id) { components.erase(id); }

			std::unordered_map<EntityID, T>& GetComponents() noexcept { return components; }
		private:
			std::unordered_map<EntityID, T> components;
		};
	}
}
