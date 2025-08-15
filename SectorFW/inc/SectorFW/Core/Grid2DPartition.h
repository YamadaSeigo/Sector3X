/*****************************************************************//**
 * @file   Grid2DPartition.h
 * @brief 2Dグリッドパーティションを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <algorithm>
#include "partition.hpp"

namespace SectorFW
{
	/**
	 * @brief 2Dグリッドパーティションを表すクラス
	 */
	class Grid2DPartition
	{
	public:
		/**
		 * @brief コンストラクタ
		 * @param chunkWidth チャンクの幅
		 * @param chunkHeight チャンクの高さ
		 * @param chunkSize チャンクのサイズ
		 */
		explicit Grid2DPartition(ChunkSizeType chunkWidth, ChunkSizeType chunkHeight, ChunkSizeType chunkSize) noexcept :
			grid(chunkWidth, chunkHeight), chunkSize(chunkSize) {
		}
		/**
		 * @brief 指定した位置に基づいてチャンクを取得します。
		 * @param location 位置（Math::Vec3f型）
		 * @param policy アウトオブバウンズポリシー
		 * @return std::optional<SpatialChunk*> チャンクへのポインタ
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location, EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::Reject) noexcept {
			// 位置に基づいてチャンクを取得するロジックを実装
			// ここではダミーの実装を返す
			ChunkSizeType x = static_cast<ChunkSizeType>(floor(location.x / chunkSize));
			ChunkSizeType y = static_cast<ChunkSizeType>(floor(location.y / chunkSize));

			if (policy == EOutOfBoundsPolicy::ClampToEdge) {
				x = std::clamp(x, ChunkSizeType(0), grid.width() - 1);
				y = std::clamp(y, ChunkSizeType(0), grid.height() - 1);
				return &grid(x, y);
			}

			if (x < 0 || x >= grid.width() || y < 0 || y >= grid.height())
				return std::nullopt;

			return &grid(x, y);
		}
		/**
		 * @brief グリッドを取得します。
		 * @return const Grid2D<SpatialChunk, ChunkSizeType>& グリッドへの参照
		 */
		const Grid2D<SpatialChunk, ChunkSizeType>& GetGrid() const noexcept {
			return grid;
		}
		/**
		 * @brief グローバルエンティティマネージャーを取得します。
		 * @return ECS::EntityManager& グローバルエンティティマネージャーへの参照
		 */
		ECS::EntityManager& GetGlobalEntityManager() noexcept {
			return globalEntityManager;
		}
	private:
		/**
		 * @brief グローバルエンティティマネージャー
		 */
		ECS::EntityManager globalEntityManager;
		/**
		 * @brief 2Dグリッド分割チャンクリスト
		 */
		Grid2D<SpatialChunk, ChunkSizeType> grid;
		/**
		 * @brief チャンクのサイズ
		 */
		ChunkSizeType chunkSize;
	};

	namespace ECS
	{
		/**
		 * @brief Grid2DPartitionのクエリを表すクラス(特殊化)
		 * @param context Grid2DPartitionのコンテキスト
		 * @return std::vector<ArchetypeChunk*> マッチするチャンクのベクター
		 */
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(Grid2DPartition& context) const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			{
				const auto& allChunk = context.GetGlobalEntityManager().GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : allChunk) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						auto& chunks = arch->GetChunks();
						result.reserve(result.size() + chunks.size());
						for (auto& chunk : chunks) {
							result.push_back(chunk.get());
						}
					}
				}
			}
			const auto& grid = context.GetGrid();
			for (const auto& spatial : grid)
			{
				const auto& allChunk = spatial.GetEntityManager().GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : allChunk) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						auto& chunks = arch->GetChunks();
						result.reserve(result.size() + chunks.size());
						for (auto& chunk : chunks) {
							result.push_back(chunk.get());
						}
					}
				}
			}
			return result;
		}
	}
}
