/*****************************************************************//**
 * @file   EntityManager.h
 * @brief エンティティマネージャーを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <queue>
#include <atomic>
#include <functional>

#include "ArchetypeManager.h"
#include "Accessor.hpp"

#include "../../Util/TypeChecker.hpp"
#include "../../Util/AccessWrapper.hpp"

#include "EntityIDAllocator.h"

#include <type_traits>
#include <new>
#include <shared_mutex>

namespace SFW
{
	class ObjectManager;

	namespace ECS
	{
		/**
		 * @brief エンティティの生成と破棄、コンポーネントの追加・削除を管理するクラス
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
			[[nodiscard]] EntityID AddEntity(const Components&... components) {
				ComponentMask mask;
				(SetMask<Components>(mask), ...);

				EntityID id = entityAllocator.Create();

				if (!id.IsValid()) return id;

				Archetype* arch = archetypeManager.GetOrCreate(mask);
				ArchetypeChunk* chunk = arch->GetOrCreateChunk();

				size_t index = chunk->AddEntity(id);

				(..., MemorySetChunk(chunk, id, index, components));

				{
					std::unique_lock lock(locationsMutex);
					locations[id] = EntityLocation{ chunk, index };
				}
				return id;
			}
			/**
			 * @brief エンティティの追加(コンポーネントマスクを指定)
			 * @param mask コンポーネントマスク
			 * @param ...components 追加するコンポーネントの可変引数
			 * @return EntityID 新しく追加されたエンティティのID
			 */
			template<typename... Components>
			[[nodiscard]] EntityID AddEntity(ComponentMask mask, const Components&... components) {
				EntityID id = entityAllocator.Create();

				if (!id.IsValid()) [[unlikely]] return id;

				Archetype* arch = archetypeManager.GetOrCreate(mask);
				ArchetypeChunk* chunk = arch->GetOrCreateChunk();

				size_t index = chunk->AddEntity(id);

				(..., MemorySetChunk(chunk, index, components));

				{
					std::unique_lock lock(locationsMutex);
					locations[id] = EntityLocation{ chunk, index };
				}
				return id;
			}
			/**
			 * @brief エンティティの削除
			 * @param id 削除するエンティティのID
			 */
			void DestroyEntity(EntityID id);

			/**
			 * @brief すべてのエンティティの削除
			 */
			void CleanAllEntity();
			/**
			 * @brief エンティティにコンポ―ネントがあるかどうかを確認する関数
			 * @param id エンティティID
			 * @return bool コンポーネントがある場合はtrue、ない場合はfalse
			 */
			template<typename T>
			bool HasComponent(EntityID id) const noexcept {
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				{
					std::shared_lock<std::shared_mutex> lock(locationsMutex);
					if (locations.contains(id)) {
						const ComponentMask& mask = GetMask(id);
						return mask.test(typeID);
					}
				}
	
				return false;
			}
			/**
			 * @brief エンティティからコンポーネントを取得する関数
			 * @param id エンティティID
			 * @details コンポーネントが見つからない場合nullptrを返す
			 * @details Systemからアクセスする場合は必ずアクセスするコンポーネントに追加する
			 * @return std::optional<T> コンポーネントの値
			 */
			template<typename T>
			std::optional<T> ReadComponent(EntityID id) noexcept {
				
				std::shared_lock lock(locationsMutex);

				auto it = locations.find(id);
				if (it == locations.end()) [[unlikely]] {
					return std::nullopt;
				}

				const auto loc = it->second;

				// SoA コンポーネント
				if constexpr (IsSoAComponent<T>) {
					ComponentAccessor<Read<T>> accessor(loc.chunk);
					auto soaPtrOpt = accessor.Get<Read<T>>();
					if (!soaPtrOpt) [[unlikely]] {
						return std::nullopt;
					}
					return ComponentAccessorBase::ConvertSoAToAoSComponent<T>(*soaPtrOpt, loc.index);
				}
				// 通常 AoS
				else {
					auto col = loc.chunk->GetColumn<T>();
					if (!col) [[unlikely]] {
						return std::nullopt;
					}
					return col.value()[loc.index];
				}
			}
			/**
			 * @brief エンティティのコンポーネントを書き換える関数
			 * @param id エンティティID
			 * @param value 書き換えるコンポーネントの値
			 * @details Systemからアクセスする場合は必ずアクセスするコンポーネントに追加する
			 */
			template<typename T>
			void WriteComponent(EntityID id, const T& value) noexcept
			{
				std::shared_lock lock(locationsMutex);

				auto it = locations.find(id);
				if (it == locations.end()) [[unlikely]] {
					return;
				}

				const auto loc = it->second;
				MemorySetChunk<T>(loc.chunk, loc.index, value);
			}

			template<typename T>
			void ReadWriteComponent(EntityID id, std::function<T(T)>&& f)
			{
				T readValue = {};

				std::shared_lock lock(locationsMutex);

				auto it = locations.find(id);
				if (it == locations.end()) [[unlikely]] {
					LOG_ERROR("EntityManager::ReadWriteComponent: Entity not found");
					return;
				}

				const auto loc = it->second;

				// SoA コンポーネント
				if constexpr (IsSoAComponent<T>) {
					ComponentAccessor<Read<T>> accessor(loc.chunk);
					auto soaPtrOpt = accessor.Get<Read<T>>();
					if (!soaPtrOpt) [[unlikely]] {
						LOG_ERROR("EntityManager::ReadWriteComponent: SoA component not found");
						return;
					}
					readValue = ComponentAccessorBase::ConvertSoAToAoSComponent<T>(*soaPtrOpt, loc.index);
				}
				// 通常 AoS
				else {
					auto col = loc.chunk->GetColumn<T>();
					if (!col) [[unlikely]] {
						LOG_ERROR("EntityManager::ReadWriteComponent: AoS component not found");
						return;
					}
					readValue = col.value()[loc.index];
				}

				auto writeValue = f(std::move(readValue));
				MemorySetChunk<T>(loc.chunk, loc.index, writeValue);
			}

			/**
			 * @brief エンティティにコンポーネントを追加する関数
			 * @param id エンティティID
			 * @details ※archetypeの移動があるため高負荷
			 * @param value 追加するコンポーネントの値
			 */
			template<typename T>
			void AddComponent(EntityID id, const T& value) {
				
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				ComponentMask currentMask = GetMask(id);
				currentMask.set(typeID);

				Archetype* newArch = archetypeManager.GetOrCreate(currentMask);
				ArchetypeChunk* newChunk = newArch->GetOrCreateChunk();

				// 旧ロケーションは共有ロックで取得（重い移動処理はロック外で実行）
				EntityLocation oldLoc;
				{
					std::shared_lock<std::shared_mutex> rlock(locationsMutex);
					oldLoc = locations.at(id);
				}
				ArchetypeChunk* oldChunk = oldLoc.chunk;
				size_t oldIndex = oldLoc.index;

				size_t newIndex = newChunk->AddEntity(id);

				// 必要なデータのみ移動
				auto& oldChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(oldChunk);
				auto& oldChunkLayoutIdx = ArchetypeChunk::Accessor::GetLayoutInfoIdx(oldChunk);
				auto& newChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(newChunk);
				auto& newChunkLayoutIdx = ArchetypeChunk::Accessor::GetLayoutInfoIdx(newChunk);
				for (const auto& [comp, idx] : oldChunkLayoutIdx) {
					auto& info = oldChunkLayout[idx];
					auto& newInfo = newChunkLayout[newChunkLayoutIdx.at(comp)];
					size_t index = 0;
					for (const auto& i : info)
					{
						auto src = ArchetypeChunk::Accessor::GetBuffer(oldChunk) + i.offset;
						auto dst = ArchetypeChunk::Accessor::GetBuffer(newChunk) + newInfo.get(index).value().get().offset;
						auto* dstElem = static_cast<uint8_t*>(dst) + newIndex * i.stride;
						auto* srcElem = static_cast<const uint8_t*>(src) + oldIndex * i.stride;
						// i.stride 単位=1要素のサイズという前提
							// メタに「列の element_size / trivial フラグ」が持てるならそれを使う。
						if (/* element is trivially copyable */ true) {
							std::memcpy(dstElem, srcElem, i.stride);
						}
						else {
							// 要素型が分からない場合は、列側に「ムーブ/コピー関数ポインタ」を持たせる設計に寄せると安全
								// new (dstElem) ElemType(*reinterpret_cast<const ElemType*>(srcElem));
								// reinterpret_cast<const ElemType*>(srcElem)->~ElemType(); // swap-pop側が破棄するなら不要
						}

						index++;
					}
				}

				newChunk->GetColumn<T>()[newIndex] = value;

				// スワップ相手のロケーション更新と自分の更新は「排他ロック」下で一気に
				{
					const size_t lastIndexBefore = oldChunk->GetEntityCount() - 1;
					std::unique_lock<std::shared_mutex> wlock(locationsMutex);
					if (oldIndex < lastIndexBefore) {
						EntityID swappedId = ArchetypeChunk::Accessor::GetEntityIDs(oldChunk)[lastIndexBefore];
						auto it = locations.find(swappedId);
						if (it != locations.end()) [[unlikely]] it->second = { oldChunk, oldIndex };
					}
					oldChunk->RemoveEntitySwapPop(oldIndex);
					locations[id] = { newChunk, newIndex };
				}
			}

			/**
			 * @brief エンティティからコンポーネントを削除する関数
			 * @details ※Archetypeを移動する処理があるので高負荷
			 * @param id エンティティID
			 */
			template<typename T>
			void RemoveComponent(EntityID id) {
				ComponentTypeID typeID = ComponentTypeRegistry::GetID<T>();
				ComponentMask oldMask = GetMask(id);
				if (!oldMask.test(typeID)) return; // 持ってないなら何もしない

				ComponentMask newMask = oldMask;
				newMask.reset(typeID);

				// 旧ロケーションは共有ロックで取得
				EntityLocation oldLoc;
				{
					std::shared_lock<std::shared_mutex> rlock(locationsMutex);
					oldLoc = locations.at(id);
				}
				ArchetypeChunk* oldChunk = oldLoc.chunk;
				size_t oldIndex = oldLoc.index;

				Archetype* newArch = archetypeManager.GetOrCreate(newMask);
				ArchetypeChunk* newChunk = newArch->GetOrCreateChunk();
				size_t newIndex = newChunk->AddEntity(id);

				// 必要なデータのみ移動
				auto& oldChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(oldChunk);
				auto& oldChunkLayoutIdx = ArchetypeChunk::Accessor::GetLayoutInfoIdx(oldChunk);
				auto& newChunkLayout = ArchetypeChunk::Accessor::GetLayoutInfo(newChunk);
				auto& newChunkLayoutIdx = ArchetypeChunk::Accessor::GetLayoutInfoIdx(newChunk);
				for (const auto& [comp, idx] : oldChunkLayout) {
					if (comp == typeID) continue;

					auto& info = oldChunkLayout[idx];
					auto& newInfo = newChunkLayout[newChunkLayoutIdx.at(comp)];
					size_t index = 0;
					for (const auto& i : info)
					{
						auto src = ArchetypeChunk::Accessor::GetBuffer(oldChunk) + i.offset;
						auto dst = ArchetypeChunk::Accessor::GetBuffer(newChunk) + newInfo.get(index).value().get().offset;
						std::memcpy(static_cast<uint8_t*>(dst) + newIndex * i.stride,
							static_cast<const uint8_t*>(src) + oldIndex * i.stride,
							i.stride);

						index++;
					}
				}

				{
					const size_t lastIndexBefore = oldChunk->GetEntityCount() - 1;
					std::unique_lock<std::shared_mutex> wlock(locationsMutex);
					if (oldIndex < lastIndexBefore) {
						EntityID swappedId = ArchetypeChunk::Accessor::GetEntityIDs(oldChunk)[lastIndexBefore];
						auto it = locations.find(swappedId);
						if (it != locations.end()) it->second = { oldChunk, oldIndex };
					}
					oldChunk->RemoveEntitySwapPop(oldIndex);
					locations[id] = { newChunk, newIndex };
				}
			}

			// src の全エンティティを this へ統合。戻り値: 移送件数
			size_t MergeFromAll(EntityManager& src);

			// ルータ: router(EntityID id, const ComponentMask& mask) -> EntityManager&
			// 返された宛先ごとに分割。戻り値: 移送件数
			template<typename Router>
			size_t SplitByAll(Router&& router) {
				const auto ids = GetAllEntityIDs(); // スナップショット
				size_t moved = 0;
				for (EntityID id : ids) {
					auto locOpt = TryGetLocation(id);
					if (!locOpt) continue;
					EntityManager* dst = router(id, locOpt->chunk->GetComponentMask());
					if (dst == nullptr)
					{
						LOG_ERROR("Router returned nullptr for EntityID { i: %d, g: %d }", id.index, id.generation);
						continue;
					}
					if (dst == this) continue;
					if (InsertWithID_ForManagerMove(id, *this, *dst)) { ++moved; }
				}
				return moved;
			}

			// すべてのエンティティIDを列挙（locations  全チャンクを補完）
			std::vector<EntityID> GetAllEntityIDs() const;

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
			/**
			 * @brief エンティティの現在位置（Chunk と行）を O(1) で取得（共有ロック）
			 */
			std::optional<EntityLocation> TryGetLocation(EntityID id) const noexcept {
				std::shared_lock<std::shared_mutex> rlock(locationsMutex);
				auto it = locations.find(id);
				if (it == locations.end()) return std::nullopt;
				return it->second;
			}

			size_t GetEntityCount() const noexcept {
				std::shared_lock<std::shared_mutex> rlock(locationsMutex);
				return locations.size();
			}
			/**
				 * @brief IDを保持したまま dst 側に1行確保し、非スパースをコピー（manager間移送用）
				 * @param id エンティティID
				 * @param src 元のエンティティマネージャー
				 * @param dst 移動先のエンティティマネージャー
				 * @return bool 移動に成功した場合はtrue、失敗した場合はfalse
				 */
			static bool InsertWithID_ForManagerMove(EntityID id, EntityManager& src, EntityManager& dst);
		private:
			/**
			 * @brief メモリ上のチャンクに値を設定する関数(SoAコンポーネントではない場合)
			 * @param chunk アーキタイプチャンクへのポインタ
			 * @param index チャンク内のインデックス
			 * @param value 設定する値
			 */
			template<typename T>
				requires (!requires { typename T::soa_type; })
			void MemorySetChunk(ArchetypeChunk* chunk, size_t index, const T& value) noexcept {
				auto column = chunk->GetColumn<T>();
				if (!column) [[unlikely]] {
					LOG_ERROR("コンポーネントタイプ{ %d }がアーキタイプチャンクに見つかりません", ComponentTypeRegistry::GetID<T>());
					return;
				}

				if constexpr (std::is_trivially_copyable_v<T>) {
					std::memcpy(&column.value()[index], &value, sizeof(T));
				}
				else if constexpr (std::is_move_constructible_v<T>) {
					// 既存オブジェクトがある想定なら明示破棄 → プレースメントnew
					column.value()[index].~T();
					new (&column.value()[index]) T(value);
				}
				else {
					static_assert(std::is_trivially_copyable_v<T>,
						"Non-trivial T must be move/copy constructible for in-place placement.");
				}
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
				if (!column) [[unlikely]] {
					LOG_ERROR("コンポーネントタイプ{ %d }がアーキタイプチャンクに見つかりません", ComponentTypeRegistry::GetID<T>());
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
				static_assert(std::is_member_object_pointer_v<decltype(ptr)>);	// メンバーオブジェクトポインタであることを確認
				auto& member = value.*ptr;

				using MemberT = std::remove_cvref_t<decltype(member)>; //constと参照を除去した型

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
			 * @brief スパースを触らずにローカルから除去（ID破棄しない）
			 * @param id 除去するエンティティのID
			 * @return bool 除去に成功した場合はtrue、失敗した場合はfalse
			 */
			bool EraseEntityLocal(EntityID id);
			/**
			 * @brief 非スパース列（チャンク列）を src->dst にコピー
			 */
			static void CopyEntityColumns(ArchetypeChunk* srcChunk, size_t srcIndex,
				ArchetypeChunk* dstChunk, size_t dstIndex);
			//エンティティIDアロケータ
			static inline EntityIDAllocator entityAllocator = EntityIDAllocator(MAX_ENTITY_NUM);
			//アーキタイプマネージャー
			ArchetypeManager archetypeManager;
			/**
			 * @brief エンティティの位置を管理するマップ
			 * @details EntityIDをキーに、EntityLocationを値とするマップ
			 */
			std::unordered_map<EntityID, EntityLocation> locations;
			//locations の並行アクセスを守るロック（読取多数・書込少数を想定）
			mutable std::shared_mutex locationsMutex;

		public:
			class EntityIDAllocatorAccessor {
				static EntityID Create() { return entityAllocator.Create(); }
				static void Destroy(EntityID id) { entityAllocator.Destroy(id); }

				friend class SFW::ObjectManager;
			};
		};
	}

	namespace ECS
	{
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(ECS::EntityManager& em) const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			const auto& all = em.GetArchetypeManager().GetAllData();
			for (const auto& arch : all) {
				const ComponentMask& mask = arch->GetMask();
				if ((mask & required) == required && (mask & excluded).none()) {
					const auto& chunks = arch->GetChunks();
					// 先に必要分だけまとめて拡張（平均的に再確保を減らす）
					result.reserve(result.size() + chunks.size());
					for (const auto& ch : chunks) {
						result.push_back(ch.get());
					}
				}
			}

			return result;
		}
	}
}
