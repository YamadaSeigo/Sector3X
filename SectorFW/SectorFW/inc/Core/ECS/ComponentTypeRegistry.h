#pragma once

#include <type_traits>

#include "component.h"

namespace SectorFW
{
	namespace ECS
	{
		template<typename, typename = void>
		struct is_sparse_component : std::false_type {};

		template<typename T>
		struct is_sparse_component<T, std::void_t<typename T::sparse_tag>>
			: std::is_same<typename T::sparse_tag, SparseComponentTag> {
		};

		template<typename T>
		constexpr bool is_sparse_component_v = is_sparse_component<T>::value;

		class ComponentTypeRegistry {
		public:
			template<typename T>
			static ComponentTypeID GetID() noexcept {
				static ComponentTypeID id = counter++;
				return id;
			}

			template<typename T>
			static void Register() noexcept {
				ComponentTypeID id = GetID<T>();
				meta[id] = { sizeof(T), alignof(T) ,is_sparse_component_v<T> };
			}

			template<typename T>
			static constexpr bool IsSparse() noexcept {
				return is_sparse_component_v<T>;
			}

			static ComponentMeta GetMeta(ComponentTypeID id) noexcept;

		private:
			static inline ComponentTypeID counter = 0;
			static inline std::unordered_map<ComponentTypeID, ComponentMeta> meta;
		};

		template<typename T>
		void SetMask(ComponentMask& mask) {
			if (!ComponentTypeRegistry::IsSparse<T>()) {
				mask.set(ComponentTypeRegistry::GetID<T>());
			}
		}
	}
}
