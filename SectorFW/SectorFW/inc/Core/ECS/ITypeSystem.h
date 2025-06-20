#pragma once

#include "ISystem.h"
#include "ComponentTypeRegistry.h"
#include "ServiceContext.h"

namespace SectorFW
{
	namespace ECS
	{
		template<typename Partition, typename AccessSpec, typename ContextSpec>
		class ITypeSystem;

		//=== ITypeSystem ===
		template<typename Partition, typename... Components, typename... Services>
		class ITypeSystem<Partition, ComponentAccess<Components...>, ServiceContext<Services...>> : public ISystem<Partition> {
		protected:
			using AccessorTuple = ComponentAccess<Components...>::Tuple;
			using ContextTuple = ServiceContext<Services...>::Tuple;

			template<typename F, typename... AccessTypes>
			void DispatchChunkWithAccessor(EntityManager& entities, F&& func, std::tuple<AccessTypes...>*) {

				/*ComponentAccessor<AccessTypes...> accessor(chunk);
				func(accessor);*/
			}

			template<typename F>
			void ForEachChunkWithAccessor(EntityManager& entities, F&& func) {
				DispatchChunkWithAccessor(entities, std::forward<F>(func), static_cast<AccessorTuple*>(nullptr));
			}

			virtual void UpdateImpl(Partition& glid, Services*... services) = 0;

		public:
			void SetContext(ContextTuple ctx) {
				context = std::move(ctx);
			}

			void Update(Partition& partition) override {
				std::apply(
					[&](Services*... unpacked) {
						this->UpdateImpl(partition, unpacked...);
					},
					context
				);
			}

			AccessInfo GetAccessInfo() const noexcept override {
				return ComponentAccess<Components...>::GetAccessInfo();
			}

		private:
			ContextTuple context;

			template<typename T>
			static void SetMask(ComponentMask& mask) {
				mask.set(ComponentTypeRegistry::GetID<T::Type>());
			}

			template<typename Tuple, std::size_t... Is>
			static ComponentMask BuildMaskFromTupleImpl(std::index_sequence<Is...>) {
				ComponentMask mask;
				(SetMask<std::tuple_element_t<Is, Tuple>>(mask), ...);
				return mask;
			}

			template<typename Tuple>
			static ComponentMask BuildMaskFromTuple() {
				return BuildMaskFromTupleImpl<Tuple>(
					std::make_index_sequence<std::tuple_size_v<Tuple>>{});
			}

			static inline ComponentMask required = BuildMaskFromTuple<AccessorTuple>();
		};
	}
}
