#pragma once

#include <unordered_set>

#include "component.h"

namespace SectorFW
{
	namespace ECS
	{
		//----------------------------------------------
		// System Interface with AccessInfo
		//----------------------------------------------
		struct AccessInfo {
			std::unordered_set<ComponentTypeID> read;
			std::unordered_set<ComponentTypeID> write;
		};

		// Access Tag Types
		template<typename T> struct Read { using Type = T; };
		template<typename T> struct Write { using Type = T; };

		// Register ComponentTypeID to AccessInfo
		template<typename T>
		void RegisterAccessType(AccessInfo& info) {
			if constexpr (std::is_base_of_v<Read<typename T::Type>, T>) {
				info.read.insert(ComponentTypeRegistry::GetID<typename T::Type>());
			}
			if constexpr (std::is_base_of_v<Write<typename T::Type>, T>) {
				info.write.insert(ComponentTypeRegistry::GetID<typename T::Type>());
			}
		}
	}
}