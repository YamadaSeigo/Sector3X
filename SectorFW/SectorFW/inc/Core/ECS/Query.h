#pragma once

#include "ArchetypeManager.h"

namespace SectorFW
{
	namespace ECS
	{
		template <typename... Args>
		concept AllDense = (!is_sparse_component_v<Args> && ...);

		template<typename T>
		concept ReferenceOnly = std::is_reference_v<T>;

		template<ReferenceOnly T>
		class Query
		{
		public:
			explicit Query(const T context) :context(context) {}

			template<typename... Ts>
				requires AllDense<Ts...>
			Query& With() noexcept {
				(required.set(ComponentTypeRegistry::GetID<Ts>()), ...);
				return *this;
			}

			template<typename... Ts>
			Query& Without() noexcept {
				(excluded.set(ComponentTypeRegistry::GetID<Ts>()), ...);
				return *this;
			}

			std::vector<ArchetypeChunk*> MatchingChunks() const noexcept;

		private:
			ComponentMask required;
			ComponentMask excluded;
			const T context;
		};

		/*template<> std::vector<ArchetypeChunk*> Query<ArchetypeManager&>::MatchingChunks() const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			for (const auto& [_, arch] : context.GetAll()) {
				const ComponentMask& mask = arch->GetMask();
				if ((mask & required) == required && (mask & excluded).none()) {
					for (auto& chunk : arch->GetChunks()) {
						result.push_back(chunk.get());
					}
				}
			}
			return result;
		}

		template<> std::vector<ArchetypeChunk*> Query<Grid2D<SpatialChunk>&>::MatchingChunks() const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			for (auto& spatial : context)
			{
				const auto& allChunk = spatial.GetEntityManger().GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : allChunk) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						for (auto& chunk : arch->GetChunks()) {
							result.push_back(chunk.get());
						}
					}
				}
			}
			return result;
		}*/
	}
}
