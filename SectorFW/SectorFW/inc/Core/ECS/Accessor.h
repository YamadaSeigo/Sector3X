#pragma once

#include "AccessInfo.h"

namespace SectorFW
{
	namespace ECS
	{
		//=== ComponentAccess ===
		template<typename... AccessTypes>
		struct ComponentAccess {
			using Tuple = std::tuple<AccessTypes...>;

			static AccessInfo GetAccessInfo() {
				AccessInfo info;
				(RegisterAccess<AccessTypes>(info), ...);
				return info;
			}

		private:
			template<typename T>
			static void RegisterAccess(AccessInfo& info) {
				if constexpr (std::is_base_of_v<Read<typename T::Type>, T>) {
					info.read.insert(ComponentTypeRegistry::GetID<typename T::Type>());
				}
				if constexpr (std::is_base_of_v<Write<typename T::Type>, T>) {
					info.write.insert(ComponentTypeRegistry::GetID<typename T::Type>());
				}
			}
		};

		//=== AccessPolicy ===
		template<typename AccessType>
		struct AccessPolicy;

		template<typename T>
		struct AccessPolicy<Read<T>> {
			using ComponentType = T;
			using PointerType = const T*;
		};

		template<typename T>
		struct AccessPolicy<Write<T>> {
			using ComponentType = T;
			using PointerType = T*;
		};

		//=== ComponentAccessor ===
		template<typename... AccessTypes>
		class ComponentAccessor {
		public:
			explicit ComponentAccessor(ArchetypeChunk& chunk) :chunk(chunk) {}

			template<typename AccessType>
			typename AccessPolicy<AccessType>::PointerType Get() const {
				//constexpr ComponentTypeID id = ComponentTypeRegistry::GetID<typename AccessPolicy<AccessType>::ComponentType>();
				return reinterpret_cast<typename AccessPolicy<AccessType>::PointerType>(chunk.GetColumn<AccessType::T>);
			}

		private:
			ArchetypeChunk& chunk;
		};
	}
}