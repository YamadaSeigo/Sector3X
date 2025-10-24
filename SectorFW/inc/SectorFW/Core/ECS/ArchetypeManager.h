/*****************************************************************//**
 * @file   ArchetypeManager.h
 * @brief アーキタイプマネージャーを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "Archetype.h"
#include "Query.h"

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief 各コンポーネントマスクごとのアーキタイプチャンクを管理するクラス
		 */
		class ArchetypeManager {
		public:
			/**
			 * @brief 対象のマスクに対応するアーキタイプを取得または作成します。
			 * @detail maskにSparseComponentを入れてはいけない
			 * @param mask 対象のコンポーネントマスク
			 * @return 対象のアーキタイプポインタ
			 */
			Archetype* GetOrCreate(const ComponentMask& mask);

			const Archetype* AccessArchetype(uint32_t idx) const {
				if (idx >= archetypeData.size()) return nullptr;

				return archetypeData[idx].get();
			}

			const std::unordered_map<ComponentMask, uint32_t>& GetAllMaskIndices() const noexcept {
				return archetypeIndices;
			}
			/**
			 * @brief すべてのアーキタイプを取得します。
			 * @return アーキタイプの配列の参照
			 */
			const std::vector<std::unique_ptr<Archetype>>& GetAllData() const noexcept {
				return archetypeData;
			}
		private:
			//アーキタイプマネージャーのアーキタイプを格納するマップ
			std::unordered_map<ComponentMask, uint32_t> archetypeIndices;
			//実際のアーキタイプのデータ
			std::vector<std::unique_ptr<Archetype>> archetypeData;
		};

		/**
		 * @brief エンティティのアーキタイプの位置を表す構造体
		 */
		struct EntityLocation {
			ArchetypeChunk* chunk;
			size_t index;
		};
		/**
		 * @brief ArchetypeManagerのクエリを表すクラス(特殊化)
		 * @param context アーキタイプマネージャーのコンテキスト
		 * @return std::vector<ArchetypeChunk*> マッチするチャンクのベクター
		 */
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(ArchetypeManager& context) const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			for (const auto& arch : context.GetAllData()) {
				const ComponentMask& mask = arch->GetMask();
				if ((mask & required) == required && (mask & excluded).none()) {
					const auto& chunks = arch->GetChunks();
					result.reserve(result.size() + chunks.size());
					for (auto& chunk : chunks) {
						result.emplace_back(chunk.get());
					}
				}
			}
			return result;
		}
	}
}