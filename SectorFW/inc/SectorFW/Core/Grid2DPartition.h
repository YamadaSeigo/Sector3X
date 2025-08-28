/*****************************************************************//**
 * @file   Grid2DPartition.h
 * @brief 2Dグリッドパーティションを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <algorithm>
#include <cmath>
#include "partition.hpp"
#include "EntityManagerRegistryService.h"
#include "../Util/Morton2D.h"

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
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location, EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept{
			// 位置に基づいてチャンクを取得するロジックを実装
			// ここではダミーの実装を返す
			using Signed = long long;
			const Signed xs = static_cast<Signed>(std::floor(static_cast<double>(location.x) / static_cast<double>(chunkSize)));
			const Signed ys = static_cast<Signed>(std::floor(static_cast<double>(location.y) / static_cast<double>(chunkSize)));
			const Signed w = static_cast<Signed>(grid.width());
			const Signed h = static_cast<Signed>(grid.height());

			if (policy == EOutOfBoundsPolicy::ClampToEdge) {
				const Signed cx = std::clamp<Signed>(xs, 0, w - 1);
				const Signed cy = std::clamp<Signed>(ys, 0, h - 1);
				return &grid(static_cast<ChunkSizeType>(cx), static_cast<ChunkSizeType>(cy));
			}

			if (xs < 0 || xs >= w || ys < 0 || ys >= h)
				return std::nullopt;

			return &grid(static_cast<ChunkSizeType>(xs), static_cast<ChunkSizeType>(ys));
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

		// ===== 初期登録：全セルを Registry に登録し、NodeKey を埋める =====
		void RegisterAllChunks(EntityManagerRegistry& reg, LevelID level) {
			const auto w = grid.width();
			const auto h = grid.height();
			for (uint32_t y = 0; y < h; ++y) {
				for (uint32_t x = 0; x < w; ++x) {
					SpatialChunk& cell = grid(x, y);
					EntityManagerKey key = MakeGrid2DKey(level, int32_t(x), int32_t(y), /*gen*/0);
					cell.SetNodeKey(key);
					reg.RegisterOwner(key, &cell.GetEntityManager());

				}
			}
		}

		// ===== セルの再ロード時：旧登録を外し、generationで再登録 =====
		void ReloadCell(uint32_t cx, uint32_t cy, EntityManagerRegistry& reg) {
			SpatialChunk& cell = grid(cx, cy);
			// 旧世代をアンレジスト
			reg.UnregisterOwner(cell.GetNodeKey());
			// 世代してキー更新・再登録
			SpatialChunk newCell = std::move(cell); // 生成し直す流儀でもOK
			newCell.BumpGeneration();
			reg.RegisterOwner(newCell.GetNodeKey(), &newCell.GetEntityManager());
			grid(cx, cy) = std::move(newCell);

		}
	private:
		inline EntityManagerKey MakeGrid2DKey(LevelID level, int32_t cx, int32_t cy,
			uint16_t gen = 0) {
			EntityManagerKey k;
			k.level = level;
			k.scheme = PartitionScheme::Grid2D;
			k.depth = 0;
			k.generation = gen;
			k.code = Morton2D64(ZigZag64(cx), ZigZag64(cy));
			return k;
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
			auto collect_from = [&](const ECS::EntityManager& em) {
				const auto& all = em.GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : all) {
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
				};

			// グローバル
			collect_from(context.GetGlobalEntityManager());
			// 空間ごと
			const auto& grid = context.GetGrid();
			for (const auto& spatial : grid) {
				collect_from(spatial.GetEntityManager());
			}
			return result;
		}
	}
}
