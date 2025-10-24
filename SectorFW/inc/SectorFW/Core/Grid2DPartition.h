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
#include "SpatialChunkRegistryService.h"
#include "../Math/sx_math.h"
#include "../Util/Morton.h"
#include "../Util/Grid.hpp"

namespace SFW
{
	/**
	 * @brief 2D(x-z)グリッドパーティションを表すクラス
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
		explicit Grid2DPartition(ChunkSizeType chunkWidth, ChunkSizeType chunkHeight, float chunkSize) noexcept :
			grid(chunkWidth, chunkHeight), chunkSize(chunkSize) {
		}
		/**
		 * @brief 指定した位置に基づいてチャンクを取得します。
		 * @param location 位置（Math::Vec3f型）
		 * @param policy アウトオブバウンズポリシー
		 * @return std::optional<SpatialChunk*> チャンクへのポインタ
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location,
			SpatialChunkRegistry& reg, LevelID level,
			EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			using Signed = long long;
			const Signed cx = static_cast<Signed>(std::floor(double(location.x) / double(chunkSize)));
			const Signed cz = static_cast<Signed>(std::floor(double(location.z) / double(chunkSize)));
			const Signed w = static_cast<Signed>(grid.width());   // X方向セル数
			const Signed d = static_cast<Signed>(grid.height());  // Z方向セル数（名はheightでも意味はDepth）

			if (policy == EOutOfBoundsPolicy::ClampToEdge) {
				const Signed ix = std::clamp<Signed>(cx, 0, w - 1);
				const Signed iz = std::clamp<Signed>(cz, 0, d - 1);
				return &grid(static_cast<ChunkSizeType>(ix), static_cast<ChunkSizeType>(iz));
			}
			if (cx < 0 || cx >= w || cz < 0 || cz >= d) return std::nullopt;

			return &grid(static_cast<ChunkSizeType>(cx), static_cast<ChunkSizeType>(cz));
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

		/**
		 * @brief 初期登録：全セルを Registry に登録し、NodeKey を埋める
		 * @param reg 登録先のレジストリ
		 * @param level レベルID
		 */
		void RegisterAllChunks(SpatialChunkRegistry& reg, LevelID level) {
			if (isRegistryChunk) return;
			isRegistryChunk = true;

			const auto w = grid.width();
			const auto h = grid.height();
			for (uint32_t y = 0; y < h; ++y) {
				for (uint32_t x = 0; x < w; ++x) {
					SpatialChunk& cell = grid(x, y);
					SpatialChunkKey key = MakeGrid2DKey(level, int32_t(x), int32_t(y), /*gen*/0);
					cell.SetNodeKey(key);
					reg.RegisterOwner(key, &cell);
				}
			}
		}
		/**
		 * @brief 全エンティティ数を取得します。
		 * @return size_t 全エンティティ数
		 */
		size_t GetEntityNum() {
			size_t num = globalEntityManager.GetEntityCount();
			const auto w = grid.width();
			const auto h = grid.height();
			for (uint32_t y = 0; y < h; ++y) {
				for (uint32_t x = 0; x < w; ++x) {
					SpatialChunk& cell = grid(x, y);
					num += cell.GetEntityManager().GetEntityCount();
				}
			}

			return num;
		}
		/**
		 * @brief フラスタムカリングを行い、可視なチャンクのリストを取得します。
		 * @param fr フラスタム
		 * @param ymin 最小Y値
		 * @param ymax 最大Y値
		 * @return std::vector<SpatialChunk*> 可視なチャンクのリスト
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr,
			float ymin = std::numeric_limits<float>::lowest(), float ymax = (std::numeric_limits<float>::max)()) const noexcept
		{
			std::vector<SpatialChunk*> out;
			const uint32_t w = grid.width(), d = grid.height();
			const float cell = float(chunkSize);
			const float exz = 0.5f * cell; // x,z は各セルの半径

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t x = 0; x < w; ++x) {
					const float cx = (x + 0.5f) * cell;
					const float cz = (z + 0.5f) * cell;

					float cyEff, eyEff;
					if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, cx, cz, ymin, ymax, cyEff, eyEff)) {
						continue; // 縦に一切重ならない → 可視になり得ない
					}

					const Math::Vec3f center{ cx,  cyEff, cz };
					const Math::Vec3f extent{ exz, eyEff, exz };

					if (fr.IntersectsAABB(center, extent)) {
						out.push_back(const_cast<SpatialChunk*>(&grid(x, z)));
					}
				}
			}
			return out;
		}
		// （任意）アロケーション回避のコールバック版
		template<class F>
		void CullChunks(const Math::Frustumf& fr, float ymin, float ymax, F&& f) const noexcept
		{
			const uint32_t w = grid.width(), d = grid.height();
			const float cell = float(chunkSize);
			const float exz = 0.5f * cell;

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t x = 0; x < w; ++x) {
					const float cx = (x + 0.5f) * cell;
					const float cz = (z + 0.5f) * cell;

					float cyEff, eyEff;
					if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, cx, cz, ymin, ymax, cyEff, eyEff)) {
						continue;
					}

					const Math::Vec3f center{ cx,  cyEff, cz };
					const Math::Vec3f extent{ exz, eyEff, exz };

					if (fr.IntersectsAABB(center, extent)) {
						f(const_cast<SpatialChunk&>(grid(x, z)));
					}
				}
			}
		}

		static inline float Dist2PointAABB3D(const Math::Vec3f& p,
			const Math::Vec3f& c, // AABB center
			const Math::Vec3f& e) // AABB extent
		{
			auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
			const float qx = clamp(p.x, c.x - e.x, c.x + e.x);
			const float qy = clamp(p.y, c.y - e.y, c.y + e.y);
			const float qz = clamp(p.z, c.z - e.z, c.z + e.z);
			const float dx = p.x - qx, dy = p.y - qy, dz = p.z - qz;
			return dx * dx + dy * dy + dz * dz;
		}

		// 可視チャンクを「カメラ位置から近い順」に上位K件返す
		std::vector<SpatialChunk*> CullChunksNear(const Math::Frustumf& fr,
			const Math::Vec3f& camPos,
			size_t maxCount = (std::numeric_limits<size_t>::max)(),
			float ymin = std::numeric_limits<float>::lowest(),
			float ymax = (std::numeric_limits<float>::max)()) const noexcept
		{
			struct Item { SpatialChunk* sc; float d2; };
			std::vector<Item> items; items.reserve(128);

			const uint32_t w = grid.width(), d = grid.height();
			const float cell = float(chunkSize);
			const float exz = 0.5f * cell;

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t x = 0; x < w; ++x) {
					const float cx = (x + 0.5f) * cell;
					const float cz = (z + 0.5f) * cell;

					float cyEff, eyEff;
					if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, cx, cz, ymin, ymax, cyEff, eyEff)) continue;

					const Math::Vec3f center{ cx, cyEff, cz };
					const Math::Vec3f extent{ exz, eyEff, exz };

					if (!fr.IntersectsAABB(center, extent)) continue;

					const float d2 = Dist2PointAABB3D(camPos, center, extent);
					items.push_back({ const_cast<SpatialChunk*>(&grid(x, z)), d2 });
				}
			}

			if (items.empty()) return {};
			const size_t K = (std::min)(maxCount, items.size());
			std::nth_element(items.begin(), items.begin() + K, items.end(),
				[](const Item& a, const Item& b) { return a.d2 < b.d2; });
			items.resize(K);
			std::sort(items.begin(), items.end(),
				[](const Item& a, const Item& b) { return a.d2 < b.d2; });

			std::vector<SpatialChunk*> out; out.reserve(K);
			for (auto& it : items) out.push_back(it.sc);
			return out;
		}

		/**
		 * @brief フラスタムカリングを行い、可視なチャンクのワイヤーフレームラインを取得します。
		 * @param fr フラスタム
		 * @param cp カメラ位置
		 * @param hy チャンクの高さの半分
		 * @param outLine 出力先のラインバッファ
		 * @param capacity ラインバッファの容量（頂点数）
		 * @param displayCount 表示距離（チャンク数）
		 * @return uint32_t 有効なライン頂点数
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			Math::Vec3f cp, float hy, Debug::LineVertex* outLine,
			uint32_t capacity, uint32_t displayCount) const noexcept
		{
			const uint32_t w = grid.width(), d = grid.height();
			const float cell = float(chunkSize);
			const float exz = 0.5f * cell; // x,z は各セルの半径

			uint32_t validCount = 0;
			float maxLength = displayCount * chunkSize;

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t x = 0; x < w; ++x) {
					const float cx = (x + 0.5f) * cell;
					const float cz = (z + 0.5f) * cell;

					Math::Vec2 vec = { cx - cp.x, cz - cp.z };
					float len = vec.length();

					if (len > maxLength) continue; // 表示距離外

					if (capacity - validCount < 6) break;

					float cyEff, eyEff;
					if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, cx, cz,
						std::numeric_limits<float>::lowest(), (std::numeric_limits<float>::max)(), cyEff, eyEff)) {
						continue; // 縦に一切重ならない → 可視になり得ない
					}

					const Math::Vec3f center{ cx, cp.y, cz };
					const Math::Vec3f extent{ exz, hy, exz };

					if (fr.IntersectsAABB(center, extent))
					{
						uint32_t rgb = Math::LerpColor(0xFFFFFFFF, 0x000000FF, len / maxLength);

						outLine[validCount + 0] = { Math::Vec3f(center.x - extent.x, center.y - extent.y, center.z - extent.z), rgb };
						outLine[validCount + 1] = { Math::Vec3f(center.x - extent.x, center.y + extent.y, center.z - extent.z), rgb };
						outLine[validCount + 2] = { Math::Vec3f(center.x + extent.x, center.y - extent.y, center.z - extent.z), rgb };
						outLine[validCount + 3] = { Math::Vec3f(center.x + extent.x, center.y + extent.y, center.z - extent.z), rgb };
						outLine[validCount + 4] = { Math::Vec3f(center.x - extent.x, center.y - extent.y, center.z + extent.z), rgb };
						outLine[validCount + 5] = { Math::Vec3f(center.x - extent.x, center.y + extent.y, center.z + extent.z), rgb };
					}

					validCount += 6;
				}
			}

			return validCount;
		}

		/**
		 * @brief セルの再ロード時：旧登録を外し、generationで再登録
		 * @param cx Xセル座標
		 * @param cy Zセル座標
		 * @param reg 登録先のレジストリ
		 */
		void ReloadCell(uint32_t cx, uint32_t cy, SpatialChunkRegistry& reg) {
			SpatialChunk& cell = grid(cx, cy);
			// 旧世代をアンレジスト
			reg.UnregisterOwner(cell.GetNodeKey());
			// 世代してキー更新・再登録
			SpatialChunk newCell = std::move(cell); // 生成し直す流儀でもOK
			newCell.BumpGeneration();
			reg.RegisterOwner(newCell.GetNodeKey(), &newCell);
			grid(cx, cy) = std::move(newCell);
		}
	private:
		/**
		 * @brief 2Dグリッドのチャンクキーを作成します。
		 * @param level レベルID
		 * @param gx Xグリッド座標
		 * @param gz Zグリッド座標
		 * @param gen 世代（デフォルトは0）
		 * @return SpatialChunkKey チャンクキー
		 */
		inline SpatialChunkKey MakeGrid2DKey(LevelID level, int32_t gx, int32_t gz, uint16_t gen = 0) {
			SpatialChunkKey k{};
			k.level = level;
			k.scheme = PartitionScheme::Grid2D;
			k.depth = 0;
			k.generation = gen;
			k.code = Morton2D64(ZigZag64(gx), ZigZag64(gz)); // ← X,Z
			return k;
		}

	private:
		//グローバルエンティティマネージャー
		ECS::EntityManager globalEntityManager;
		//2Dグリッド分割チャンクリスト
		Grid2D<SpatialChunk, ChunkSizeType> grid;
		//チャンクのサイズ
		float chunkSize;
		//チャンクを登録したか
		bool isRegistryChunk = false;
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
