#pragma once

#include <queue>
#include <atomic>

#include "ArchetypeManager.h"
#include "SparseComponentStore.h"

#include "Util/AccessWrapper.h"

#include "EntityIDAllocator.h"

namespace SectorFW
{
	namespace ECS
	{
		template <typename T>
		concept SparseComponent = is_sparse_component_v<T>;

		//----------------------------------------------
		// Entity Manager
		//----------------------------------------------
		class EntityManager {
		public:
			EntityManager() = default;

			template<typename... Components>
			EntityID AddEntity(const Components&... components) {
				ComponentMask mask;
				(SetMask<Components>(mask), ...);

				EntityID id = entityAllocator.Create();

				if (!id.IsValid()) return id;

				Archetype* arch = archetypeManager.GetOrCreate(mask);
				ArchetypeChunk* chunk = arch->GetOrCreateChunk();

				size_t index = chunk->AddEntity(id);

				(..., StoreComponent(chunk, id, index, components));
				return id;
			}

			template<typename... Components>
			EntityID AddEntity(ComponentMask mask, const Components&... components) {
				EntityID id = entityAllocator.Create();

				if (!id.IsValid()) return id;

				Archetype* arch = archetypeManager.GetOrCreate(mask);
				ArchetypeChunk* chunk = arch->GetOrCreateChunk();

				size_t index = chunk->AddEntity(id);

				(..., StoreComponent(chunk, id, index, components));
				return id;
			}

			void DestroyEntity(EntityID id);

			template<typename T>
			bool HasComponent(EntityID id) const {
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				if (locations.contains(id)) {
					const ComponentMask& mask = GetMask(id);
					return mask.test(typeID);
				}
				if (ComponentTypeRegistry::IsSparse<T>()) {
					return GetSparseStore<T>().Has(id);
				}

				return false;
			}

			template<typename T>
			T* GetComponent(EntityID id) {
				if (ComponentTypeRegistry::IsSparse<T>()) {
					return GetSparseStore<T>().Get(id);
				}

				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();

				if (!locations.contains(id)) return nullptr;
				auto& loc = locations.at(id);
				return &loc.chunk->GetColumn<T>()[loc.index];
			}

			template<typename T>
			void AddComponent(EntityID id, const T& value) {
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				ComponentMask currentMask = GetMask(id);
				currentMask.set(typeID);

				if (ComponentTypeRegistry::IsSparse<T>()) {
					GetSparseStore<T>().Add(id, value);
					return;
				}

				Archetype* newArch = archetypeManager.GetOrCreate(currentMask);
				ArchetypeChunk* newChunk = newArch->GetOrCreateChunk();

				EntityLocation oldLoc = locations.at(id);
				ArchetypeChunk* oldChunk = oldLoc.chunk;
				size_t oldIndex = oldLoc.index;

				size_t newIndex = newChunk->AddEntity(id);

				// 必要なデータのみ移動
				auto& oldChunkLayout = ArchetypeChunk::LayoutAccess::GetLayout(oldChunk);
				auto& newChunkLayout = ArchetypeChunk::LayoutAccess::GetLayout(newChunk);
				for (const auto& [comp, info] : oldChunkLayout) {
					const void* src = &info;
					void* dst = &newChunkLayout.at(comp);
					std::memcpy(static_cast<uint8_t*>(dst) + newIndex * info.stride,
						static_cast<const uint8_t*>(src) + oldIndex * info.stride,
						info.stride);
				}

				newChunk->GetColumn<T>()[newIndex] = value;

				oldChunk->RemoveEntitySwapPop(oldIndex);
				locations[id] = { newChunk, newIndex };
			}

			// Remove a component from an entity (Archetype-aware)
			template<typename T>
			void RemoveComponent(EntityID id) {
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				ComponentMask oldMask = GetMask(id);
				if (!oldMask.test(typeID)) return; // 持ってないなら何もしない

				ComponentMask newMask = oldMask;
				newMask.reset(typeID);

				if (ComponentTypeRegistry::IsSparseMask(ComponentMask().set(typeID))) {
					GetSparseStore<T>().Remove(id);
					return;
				}

				EntityLocation oldLoc = locations.at(id);
				ArchetypeChunk* oldChunk = oldLoc.chunk;
				size_t oldIndex = oldLoc.index;

				Archetype* newArch = archetypeManager.GetOrCreate(newMask);
				ArchetypeChunk* newChunk = newArch->GetOrCreateChunk();
				size_t newIndex = newChunk->AddEntity(id);

				// 必要なデータのみ移動
				auto& oldChunkLayout = ArchetypeChunk::LayoutAccess::GetLayout(oldChunk);
				auto& newChunkLayout = ArchetypeChunk::LayoutAccess::GetLayout(newChunk);
				for (const auto& [comp, info] : oldChunkLayout) {
					if (comp == typeID) continue;
					const void* src = &info;
					void* dst = &newChunkLayout.at(comp);
					std::memcpy(static_cast<uint8_t*>(dst) + newIndex * info.stride,
						static_cast<const uint8_t*>(src) + oldIndex * info.stride,
						info.stride);
				}

				oldChunk->RemoveEntitySwapPop(oldIndex);
				locations[id] = { newChunk, newIndex };
			}

			template<typename T>
				requires SparseComponent<T>
			ReadWriteView<std::unordered_map<EntityID, T>> GetSparseComponents() {
				ReadWriteView<std::unordered_map<EntityID, T>> components(GetSparseStore<T>().GetComponents());
				return components;
			}

			ComponentMask GetMask(EntityID id) const noexcept;

			const ArchetypeManager& GetArchetypeManager() const noexcept { return archetypeManager; }

			static const EntityIDAllocator& GetEntityAllocator() noexcept { return entityAllocator; }

		private:

			template<typename T>
			void StoreComponent(ArchetypeChunk* chunk, EntityID id, size_t index, const T& value) {
				if (ComponentTypeRegistry::IsSparse<T>()) {
					GetSparseStore<T>().Add(id, value);
				}
				else {
					T* column = chunk->GetColumn<T>();
					std::memcpy(&column[index], &value, sizeof(T));
				}
			}

			static inline EntityIDAllocator entityAllocator = EntityIDAllocator(MAX_ENTITY_NUM);
			ArchetypeManager archetypeManager;
			std::unordered_map<EntityID, EntityLocation> locations;

			struct ISparseWrapper {
				virtual void Remove(EntityID id) = 0;
				virtual ~ISparseWrapper() = default;
			};

			std::unordered_map<ComponentTypeID, std::shared_ptr<struct ISparseWrapper>> sparseStores;

			template<typename T>
			struct SparseWrapper : ISparseWrapper {
				SparseComponentStore<T> store;
				void Remove(EntityID id) override { store.Remove(id); }
			};

			template<typename T>
			SparseComponentStore<T>& GetSparseStore() {
				ComponentTypeID id = ComponentTypeRegistry::GetID<T>();
				if (!sparseStores.contains(id)) {
					auto wrapper = std::make_shared<SparseWrapper<T>>();
					sparseStores[id] = wrapper;
				}
				return static_cast<SparseWrapper<T>*>(sparseStores[id].get())->store;
			}
		};
	}
}