/*****************************************************************//**
 * @file   EntityManager.h
 * @brief エンティティマネージャーを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <queue>
#include <atomic>

#include "ArchetypeManager.h"
#include "SparseComponentStore.h"

#include "Util/TypeChecker.hpp"
#include "Util/AccessWrapper.hpp"

#include "EntityIDAllocator.h"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief エンティティマネージャーを表すクラス
		 */
		class EntityManager {
		public:
			/**
			 * @brief コンストラクタ
			 */
			EntityManager() = default;
			/**
			 * @brief エンティティの追加
			 * @param ...components 追加するコンポーネントの可変引数
			 * @return EntityID 新しく追加されたエンティティのID
			 */
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
			/**
			 * @brief エンティティの追加(コンポーネントマスクを指定)
			 * @param mask コンポーネントマスク
			 * @param ...components 追加するコンポーネントの可変引数
			 * @return EntityID 新しく追加されたエンティティのID
			 */
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
			/**
			 * @brief エンティティの削除
			 * @param id 削除するエンティティのID
			 */
			void DestroyEntity(EntityID id);
			/**
			 * @brief エンティティにコンポ―ネントがあるかどうかを確認する関数
			 * @param id エンティティID
			 * @return bool コンポーネントがある場合はtrue、ない場合はfalse
			 */
			template<typename T>
			bool HasComponent(EntityID id) const noexcept{
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
			/**
			 * @brief エンティティからコンポーネントを取得する関数
			 * @param id エンティティID
			 * @detail コンポーネントが見つからない場合nullptrを返す
			 * @return T* コンポーネントのポインタ
			 */
			template<typename T>
			T* GetComponent(EntityID id) noexcept{
				if (ComponentTypeRegistry::IsSparse<T>()) {
					return GetSparseStore<T>().Get(id);
				}

				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();

				if (!locations.contains(id)) return nullptr;
				auto& loc = locations.at(id);
				return &loc.chunk->GetColumn<T>()[loc.index];
			}
			/**
			 * @brief エンティティにコンポーネントを追加する関数
			 * @param id エンティティID
			 * @detail archetypeの移動があるため高負荷
			 * @param value 追加するコンポーネントの値
			 */
			template<typename T>
			void AddComponent(EntityID id, const T& value) {
				if (ComponentTypeRegistry::IsSparse<T>()) {
					GetSparseStore<T>().Add(id, value);
					return;
				}

				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				ComponentMask currentMask = GetMask(id);
				currentMask.set(typeID);

				Archetype* newArch = archetypeManager.GetOrCreate(currentMask);
				ArchetypeChunk* newChunk = newArch->GetOrCreateChunk();

				EntityLocation oldLoc = locations.at(id);
				ArchetypeChunk* oldChunk = oldLoc.chunk;
				size_t oldIndex = oldLoc.index;

				size_t newIndex = newChunk->AddEntity(id);

				// 必要なデータのみ移動
				auto& oldChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(oldChunk);
				auto& newChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(newChunk);
				for (const auto& [comp, info] : oldChunkLayout) {
					size_t index = 0;
					for (const auto& i : info)
					{
						auto src = ArchetypeChunk::Accessor::GetBuffer(oldChunk) + i.offset;
						auto dst = ArchetypeChunk::Accessor::GetBuffer(newChunk) + newChunkLayout.at(comp).get(index).offset;
						std::memcpy(static_cast<uint8_t*>(dst) + newIndex * i.stride,
							static_cast<const uint8_t*>(src) + oldIndex * i.stride,
							i.stride);

						index++;
					}
				}

				newChunk->GetColumn<T>()[newIndex] = value;

				oldChunk->RemoveEntitySwapPop(oldIndex);
				locations[id] = { newChunk, newIndex };
			}

			/**
			 * @brief エンティティからコンポーネントを削除する関数
			 * @detail Archetypeを移動する処理があるので高負荷
			 * @param id エンティティID
			 */
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
				const auto& oldChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(oldChunk);
				const auto& newChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(newChunk);
				for (const auto& [comp, info] : oldChunkLayout) {
					if (comp == typeID) continue;

					size_t index = 0;
					for (const auto& i : info)
					{
						auto src = ArchetypeChunk::Accessor::GetBuffer(oldChunk) + i.offset;
						auto dst = ArchetypeChunk::Accessor::GetBuffer(newChunk) + newChunkLayout.at(comp).get(index).offset;
						std::memcpy(static_cast<uint8_t*>(dst) + newIndex * i.stride,
							static_cast<const uint8_t*>(src) + oldIndex * i.stride,
							i.stride);

						index++;
					}
				}

				oldChunk->RemoveEntitySwapPop(oldIndex);
				locations[id] = { newChunk, newIndex };
			}
			/**
			 * @brief まばらなコンポーネントを取得する関数
			 * @return ReadWriteView<std::unordered_map<EntityID, T>> まばらなコンポーネントのビュー
			 */
			template<typename T>
				requires SparseComponent<T>
			ReadWriteView<std::unordered_map<EntityID, T>> GetSparseComponents() {
				ReadWriteView<std::unordered_map<EntityID, T>> components(GetSparseStore<T>().GetComponents());
				return components;
			}
			/**
			 * @brief エンティティのコンポーネントマスクを取得する関数
			 * @param id エンティティID
			 * @return ComponentMask エンティティのコンポーネントマスク
			 */
			ComponentMask GetMask(EntityID id) const noexcept;
			/**
			 * @brief アーキタイプマネージャーを取得する関数
			 * @return const ArchetypeManager& アーキタイプマネージャーへの参照
			 */
			const ArchetypeManager& GetArchetypeManager() const noexcept { return archetypeManager; }
			/**
			 * @brief エンティティIDアロケータを取得する関数
			 * @return const EntityIDAllocator& エンティティIDアロケータへの参照
			 */
			static const EntityIDAllocator& GetEntityAllocator() noexcept { return entityAllocator; }
		private:
			/**
			 * @brief メモリ上のチャンクに値を設定する関数(SoAコンポーネントではない場合)
			 * @param chunk アーキタイプチャンクへのポインタ
			 * @param index チャンク内のインデックス
			 * @param value 設定する値
			 */
			template<typename T>
				requires (!requires { typename T::soa_type; })
			void MemorySetChunk(ArchetypeChunk* chunk, size_t index, const T& value) noexcept{
				auto column = chunk->GetColumn<T>();
				if (!column) return;

				std::memcpy(&column.value()[index], &value, sizeof(T));
			}
			/**
			 * @brief メモリ上のチャンクに値を設定する関数(SoAコンポーネントの場合)
			 * @param chunk アーキタイプチャンクへのポインタ
			 * @param index チャンク内のインデックス
			 * @param value 設定する値
			 */
			template<typename T>
				requires IsSoAComponent<T>
			void MemorySetChunk(ArchetypeChunk* chunk, size_t index, const T& value) {
				auto column = chunk->GetColumn<T>();
				if (!column) {
					LOG_ERROR("Column for component type %d not found in chunk", ComponentTypeRegistry::GetID<T>());
					return;
				}
				size_t capacity = chunk->GetCapacity();
				MemorySetChunkSoA<T, typename T::soa_type>(
					column.value(), index, value, capacity,
					std::make_index_sequence<std::tuple_size_v<typename T::soa_type>>{}
				);
			}
			/**
			 * @brief SoAコンポーネントのメンバーを展開して値を取得する関数
			 * @param vec 値を格納するベクター
			 * @param value SoAコンポーネントの値
			 */
			template<typename Variant, typename U, size_t Index>
			void ExpansionMemberImpl(std::vector<Variant>& vec, U& value) {
				auto ptr = std::get<Index>(U::member_ptr_tuple);
				static_assert(std::is_member_object_pointer_v<decltype(ptr)>);
				auto& member = value.*ptr;

				using MemberT = std::remove_cvref_t<decltype(member)>;

				if constexpr (IsSoAComponent<MemberT>) {
					ExpansionMember<Variant>(vec, member, std::make_index_sequence<std::tuple_size_v<typename MemberT::tuple_type>>{});
				}
				else if constexpr (IsPrimitive<MemberT>) {
					vec.push_back(member);
				}
				else {
					static_assert(std::is_same_v<MemberT, void>, "Unsupported member type: must be primitive or reflectable (HasMemberPtr)");
				}
			}
			/**
			 * @brief SoAコンポーネントのメンバーを展開して値を取得する関数(インデックスシーケンスを使用)
			 * @param vec 値を格納するベクター
			 * @param value SoAコンポーネントの値
			 * @param Is インデックスシーケンス
			 */
			template<typename Variant, typename U, size_t... Is>
			void ExpansionMember(std::vector<Variant>& vec, U& value, std::index_sequence<Is...>) {
				(ExpansionMemberImpl<Variant, U, Is>(vec, value), ...);
			}
			/**
			 * @brief SoAコンポーネントのメンバーを展開して値を取得する関数
			 * @param t SoAコンポーネントの値
			 * @return std::vector<typename T::variant_type> SoAコンポーネントのメンバーの値を格納したベクター
			 */
			template<IsSoAComponent T>
			auto GetMember(T& t) -> std::vector<typename T::variant_type> {
				std::vector<typename T::variant_type> member_values;
				ExpansionMember<typename T::variant_type>(member_values, t, std::make_index_sequence<std::tuple_size_v<typename T::tuple_type>>{});
				return member_values;
			}
			/**
			 * @brief SoAコンポーネントのメモリ上のチャンクに値を設定する関数
			 * @param base SoAコンポーネントのベースポインタ
			 * @param index チャンク内のインデックス
			 * @param value 設定する値
			 * @param capacity チャンクの容量
			 * @param Is インデックスシーケンス
			 */
			template<typename T, typename SoATuple, std::size_t... Is>
			void MemorySetChunkSoA(T* base, size_t index, const T& value, size_t capacity, std::index_sequence<Is...>) {
				auto charBase = reinterpret_cast<BufferType*>(base);
				size_t offset = 0;
				auto members = GetMember(value);
				for (const auto& v : members) {
					std::visit([&](auto&& val) {
						using ValType = std::remove_cvref_t<decltype(val)>;
						offset = AlignTo(offset, alignof(ValType));
						*(reinterpret_cast<ValType*>(charBase + offset) + index) = val;
						offset += sizeof(ValType) * capacity;
						}, v);
				}
			}
			/**
			 * @brief コンポーネントをチャンクに格納する関数
			 * @param chunk アーキタイプチャンクへのポインタ
			 * @param id エンティティID
			 * @param index チャンク内のインデックス
			 * @param value 格納するコンポーネントの値
			 * @detail まばらなコンポーネントかどうかで処理を分ける
			 */
			template<typename T>
			void StoreComponent(ArchetypeChunk* chunk, EntityID id, size_t index, const T& value) {
				if (ComponentTypeRegistry::IsSparse<T>()) {
					GetSparseStore<T>().Add(id, value);
				}
				else {
					MemorySetChunk<T>(chunk, index, value);
				}
			}
			/**
			 * @brief エンティティIDアロケータ
			 */
			static inline EntityIDAllocator entityAllocator = EntityIDAllocator(MAX_ENTITY_NUM);
			/**
			 * @brief アーキタイプマネージャー
			 */
			ArchetypeManager archetypeManager;
			/**
			 * @brief エンティティの位置を管理するマップ
			 * @detail EntityIDをキーに、EntityLocationを値とするマップ
			 */
			std::unordered_map<EntityID, EntityLocation> locations;
			/**
			 * @brief まばらなコンポーネントストアを取得するためのインターフェース
			 */
			struct ISparseWrapper {
				virtual void Remove(EntityID id) = 0;
				virtual ~ISparseWrapper() = default;
			};
			/**
			 * @brief まばらなコンポーネントストアを格納するマップ
			 */
			std::unordered_map<ComponentTypeID, std::shared_ptr<struct ISparseWrapper>> sparseStores;
			/**
			 * @brief まばらなコンポーネントストアのラッパークラス
			 */
			template<typename T>
			struct SparseWrapper : ISparseWrapper {
				SparseComponentStore<T> store;
				void Remove(EntityID id) override { store.Remove(id); }
			};
			/**
			 * @brief まばらなコンポーネントストアを取得する関数
			 * @return SparseComponentStore<T>& まばらなコンポーネントストアへの参照
			 */
			template<typename T>
			SparseComponentStore<T>& GetSparseStore() noexcept{
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