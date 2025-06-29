/*****************************************************************//**
 * @file   Archetype.h
 * @brief アーキタイプを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <vector>
#include <memory>

#include "ArchetypeChunk.h"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief アーキタイプを表すクラス(マスクベースのコンポーネント管理)
		 */
		class Archetype {
		public:
			/**
			 * @brief コンストラクタ
			 * @param mask コンポーネントマスク
			 */
			explicit Archetype(ComponentMask mask) noexcept : mask(mask) {}
			/**
			 * @brief アーキタイプのチャンクを取得または作成する関数
			 * @return ArchetypeChunk* アーキタイプチャンクへのポインタ
			 */
			ArchetypeChunk* GetOrCreateChunk() {
				for (auto& chunk : chunks) {
					if (chunk->GetEntityCount() < chunk->GetCapacity())
						return chunk.get();
				}
				std::unique_ptr<ArchetypeChunk> newChunk = std::make_unique<ArchetypeChunk>(mask);
				ArchetypeChunk* ptr = newChunk.get();
				chunks.emplace_back(std::move(newChunk));

				return ptr;
			}
			/**
			 * @brief アーキタイプのコンポーネントマスクを取得する関数
			 * @return const ComponentMask& コンポーネントマスクへの参照
			 */
			const ComponentMask& GetMask() const noexcept { return mask; }
			/**
			 * @brief アーキタイプのチャンクを取得する関数
			 * @return const std::vector<std::unique_ptr<ArchetypeChunk>>& チャンクのベクターへの参照
			 */
			const std::vector<std::unique_ptr<ArchetypeChunk>>& GetChunks() const noexcept {
				return chunks;
			}
		private:
			/**
			 * @brief アーキタイプのコンポーネントマスク
			 */
			ComponentMask mask;
			/**
			 * @brief アーキタイプのチャンクのベクター
			 */
			std::vector<std::unique_ptr<ArchetypeChunk>> chunks;
		};
	}
}
