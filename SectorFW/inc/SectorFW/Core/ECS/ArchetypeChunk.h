/*****************************************************************//**
 * @file   ArchetypeChunk.h
 * @brief アーキタイプチャンクを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <cassert>
#include <bit>

#include "entity.h"
#include "ComponentTypeRegistry.h"
#include "ComponentLayoutRegistry.h"

#include "Util/alignment.h"
#include "Debug/logger.h"

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief アーキタイプチャンクのサイズ（バイト単位）
		 */
		constexpr size_t ChunkSizeBytes = 32 * 1024;
		//前方定義
		class ComponentTypeRegistry;
		/**
		 * @brief アーキタイプチャンクのバッファ型
		 */
		using BufferType = uint8_t;
		/**
		 * @brief 対象のアーキタイプのコンポーネントのデータを格納するクラス
		 */
		class ArchetypeChunk {
		public:
			/**
			 * @brief コンストラクタ
			 * @param mask コンポーネントマスク
			 * @details maskからレイアウトを取得し、チャンクの容量を設定します。
			 */
			ArchetypeChunk(ComponentMask mask) : layout(ComponentLayoutRegistry::GetLayout(mask)),
				componentMask(mask) {
				assert(layout.capacity > 0 && "Chunk capacity must be greater than 0");
				entities.resize(layout.capacity);
			}
			/**
			 * @brief コンポーネントの列を取得する関数
			 * @return std::optional<T*> コンポーネントのポインタ
			 */
			template<typename T>
			std::optional<T*> GetColumn() noexcept {
				ComponentTypeID id = ComponentTypeRegistry::GetID<T>();
				auto it = layout.infoIdx.find(id);
				if (it == layout.infoIdx.end()) [[unlikely]] {
					LOG_ERROR("コンポーネントID{ %d }がレイアウトに見つかりません", id);
					return std::nullopt; // or throw an exception
				}
				auto info = layout.info[it->second].get(0);
				if (!info) [[unlikely]] {
					LOG_ERROR("コンポーネントID{ %d }がレイアウト情報に見つかりません", id);
					return std::nullopt;
				}

				return reinterpret_cast<T*>(&buffer[info.value().get().offset]);
			}
			/**
			 * @brief エンティティを追加する関数
			 * @param id エンティティID
			 * @return size_t 追加されたエンティティのインデックス
			 */
			size_t AddEntity(EntityID id);
			/**
			 * @brief エンティティを削除する関数(最後のエンティティと入れ替えて追放)
			 * @param index 削除するエンティティのインデックス
			 */
			void RemoveEntitySwapPop(size_t index) noexcept;
			/**
			 * @brief エンティティの数を取得する関数
			 * @return size_t エンティティの数
			 */
			size_t GetEntityCount() const noexcept { return entityCount; }
			/**
			 * @brief チャンクの容量を取得する関数
			 * @return size_t チャンクの容量
			 */
			size_t GetCapacity() const noexcept { return layout.capacity; }
			/**
			 * @brief コンポーネントマスクを取得する関数
			 * @return ComponentMask コンポーネントマスク
			 */
			ComponentMask GetComponentMask() const noexcept { return componentMask; }
			/**
			 * @brief エンティティIDの配列を取得する関数
			 * @return const std::vector<EntityID>& エンティティIDの配列への参照
			 */
			const std::vector<EntityID>& GetEntities() const noexcept { return entities; }
		private:
			//アーキタイプチャンクのバッファ
			BufferType buffer[ChunkSizeBytes] = {};
			//エンティティの数
			size_t entityCount = 0;
			//エンティティのIDを格納するベクター
			std::vector<EntityID> entities;
			//コンポーネントマスク
			ComponentMask componentMask;
			//コンポーネントレイアウト
			const ComponentLayout& layout;
		public:
			/**
			 * @brief アーキタイプチャンクのアクセサークラス
			 */
			struct Accessor
			{
			private:
				/**
				 * @brief アーキタイプチャンクバッファへのポインタ
				 * @param chunk アーキタイプチャンクへのポインタ
				 * @return BufferType* バッファへのポインタ
				 */
				static auto GetBuffer(ArchetypeChunk* chunk) noexcept { return chunk->buffer; }
				/**
				 * @brief エンティティの配列を取得する関数
				 * @param chunk アーキタイプチャンクへのポインタ
				 * @return const std::vector<EntityID>& エンティティの配列への参照
				 */
				static const std::vector<EntityID>& GetEntities(ArchetypeChunk* chunk) noexcept { return chunk->entities; }
				/**
				 * @brief コンポーネントレイアウト情報を取得する関数
				 * @param chunk アーキタイプチャンクへのポインタ
				 * @return const decltype(ComponentLayout::info)& コンポーネントレイアウト情報への参照
				 */
				static const decltype(ComponentLayout::info)& GetLayoutInfo(ArchetypeChunk* chunk) noexcept { return chunk->layout.info; }
				/**
				 * @brief コンポーネントレイアウト情報のインデックスを取得する関数
				 * @param chunk アーキタイプチャンクへのポインタ
				 * @return const decltype(ComponentLayout::infoIdx)& コンポーネントレイアウト情報のインデックスへの参照
				 */
				static const decltype(ComponentLayout::infoIdx)& GetLayoutInfoIdx(ArchetypeChunk* chunk) noexcept { return chunk->layout.infoIdx; }

				friend class EntityManager;
			};
		};
	}
}
