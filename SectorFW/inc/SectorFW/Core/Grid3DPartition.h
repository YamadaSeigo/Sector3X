/*****************************************************************//**
 * @file   Grid3DPartition.h
 * @brief  3Dグリッドパーティションを定義するクラス（PartitionConcept 準拠）
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "partition.hpp"                   // PartitionConcept / SpatialChunk / ChunkSizeType / EOutOfBoundsPolicy など
#include "SpatialChunkRegistryService.h"  // EntityManagerRegistry / EntityManagerKey / LevelID
#include "../Util/Grid.hpp"                // Grid3D<T, N>
#include "../Util/Morton.h"              // Morton3D64, ZigZag64
#include "../Math/sx_math.h"

namespace SFW
{
	/**
	 * @brief 3Dグリッドパーティションを定義するクラス（PartitionConcept 準拠）
	 */
	class Grid3DPartition
	{
	public:
		/**
		 * @brief コンストラクタ
		 * @param chunkWidth  X方向セル数
		 * @param chunkHeight Y方向セル数
		 * @param chunkSize   各セルの一辺（ワールド単位）
		 */
		explicit Grid3DPartition(ChunkSizeType chunkWidth,
			ChunkSizeType chunkHeight,
			float chunkSize) noexcept
			: grid(chunkWidth, chunkHeight, (chunkWidth + chunkHeight) / 2)
			, chunkSize(chunkSize) {
		}

		/**
		 * @brief コンストラクタ
		* @param chunkWidth  X方向セル数
		* @param chunkHeight Y方向セル数
		* @param chunkDepth  Z方向セル数
		* @param chunkSize   各セルの一辺（ワールド単位）
		*/
		explicit Grid3DPartition(ChunkSizeType chunkWidth,
			ChunkSizeType chunkHeight,
			ChunkSizeType chunkDepth,
			float chunkSize) noexcept
			: grid(chunkWidth, chunkHeight, chunkDepth)
			, chunkSize(chunkSize) {
		}

		/**
		 * @brief 指定位置が属するセル（SpatialChunk）を取得
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location,
			SpatialChunkRegistry& reg, LevelID level,
			EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			using Signed = long long;
			const Signed cx = static_cast<Signed>(std::floor(double(location.x) / double(chunkSize)));
			const Signed cy = static_cast<Signed>(std::floor(double(location.y) / double(chunkSize)));
			const Signed cz = static_cast<Signed>(std::floor(double(location.z) / double(chunkSize)));

			const Signed w = static_cast<Signed>(grid.width());   // X
			const Signed h = static_cast<Signed>(grid.height());  // Y
			const Signed d = static_cast<Signed>(grid.depth());   // Z

			if (policy == EOutOfBoundsPolicy::ClampToEdge) {
				const Signed ix = std::clamp<Signed>(cx, 0, w - 1);
				const Signed iy = std::clamp<Signed>(cy, 0, h - 1);
				const Signed iz = std::clamp<Signed>(cz, 0, d - 1);
				return &grid(static_cast<ChunkSizeType>(ix), static_cast<ChunkSizeType>(iy), static_cast<ChunkSizeType>(iz));
			}
			if (cx < 0 || cx >= w || cy < 0 || cy >= h || cz < 0 || cz >= d) return std::nullopt;
			return &grid(static_cast<ChunkSizeType>(cx), static_cast<ChunkSizeType>(cy), static_cast<ChunkSizeType>(cz));
		}

		/** @brief グリッド参照 */
		const Grid3D<SpatialChunk, ChunkSizeType>& GetGrid() const noexcept { return grid; }
		/** @brief グローバルEM参照 */
		ECS::EntityManager& GetGlobalEntityManager() noexcept { return globalEntityManager; }

		/**
		 * @brief すべてのセルをRegistryに登録し NodeKey を埋める
		 */
		void RegisterAllChunks(SpatialChunkRegistry& reg, LevelID level)
		{
			if (isRegistryChunk) return;
			isRegistryChunk = true;

			const auto w = grid.width();
			const auto h = grid.height();
			const auto d = grid.depth();
			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						SpatialChunk& cell = grid(x, y, z);
						SpatialChunkKey key = MakeGrid3DKey(level, int32_t(x), int32_t(y), int32_t(z), /*gen*/0);
						cell.SetNodeKey(key);
						reg.RegisterOwner(key, &cell);
					}
				}
			}
		}

		/**
		 * @brief 全エンティティ数（グローバル＋全セル）
		 */
		size_t GetEntityNum()
		{
			size_t num = globalEntityManager.GetEntityCount();
			const auto w = grid.width();
			const auto h = grid.height();
			const auto d = grid.depth();
			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						num += grid(x, y, z).GetEntityManager().GetEntityCount();
					}
				}
			}
			return num;
		}

		/**
		 * @brief 視錐台カリング（各セルを立方AABBで判定）
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr) const noexcept
		{
			std::vector<SpatialChunk*> out;
			const uint32_t w = grid.width(), h = grid.height(), d = grid.depth();
			const float cell = float(chunkSize);
			const float e = 0.5f * cell; // 半径（各軸同じ）

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						const Math::Vec3f center{ (x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell };
						const Math::Vec3f extent{ e, e, e };
						if (fr.IntersectsAABB(center, extent)) {
							auto& chunk = grid(x, y, z);
							if (chunk.GetEntityManager().GetEntityCount() > 0)
								out.push_back(const_cast<SpatialChunk*>(&chunk));
						}
					}
				}
			}
			return out;
		}

		/**
		 * @brief アロケーション回避版（コールバック受け取り）
		 */
		template<class F>
		void CullChunks(const Math::Frustumf& fr, F&& f) const noexcept
		{
			const uint32_t w = grid.width(), h = grid.height(), d = grid.depth();
			const float cell = float(chunkSize);
			const float e = 0.5f * cell;
			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						const Math::Vec3f center{ (x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell };
						const Math::Vec3f extent{ e, e, e };
						if (fr.IntersectsAABB(center, extent)) {
							auto& chunk = grid(x, y, z);
							if (chunk.GetEntityManager().GetEntityCount() > 0)
								f(const_cast<SpatialChunk&>(chunk));
						}
					}
				}
			}
		}

		static inline float Dist2PointAABB3D(const Math::Vec3f& p,
			const Math::Vec3f& c,
			const Math::Vec3f& e)
		{
			auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
			const float qx = clamp(p.x, c.x - e.x, c.x + e.x);
			const float qy = clamp(p.y, c.y - e.y, c.y + e.y);
			const float qz = clamp(p.z, c.z - e.z, c.z + e.z);
			const float dx = p.x - qx, dy = p.y - qy, dz = p.z - qz;
			return dx * dx + dy * dy + dz * dz;
		}

		std::vector<SpatialChunk*> CullChunksNear(const Math::Frustumf& fr,
			const Math::Vec3f& camPos,
			size_t maxCount = (std::numeric_limits<size_t>::max)()) const noexcept
		{
			struct Item { SpatialChunk* sc; float d2; };
			std::vector<Item> items; items.reserve(128);

			const uint32_t w = grid.width(), h = grid.height(), d = grid.depth();
			const float cell = float(chunkSize);
			const float e = 0.5f * cell;

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						const Math::Vec3f center{ (x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell };
						const Math::Vec3f extent{ e, e, e };
						if (!fr.IntersectsAABB(center, extent)) continue;

						const float d2 = Dist2PointAABB3D(camPos, center, extent);
						auto& chunk = grid(x, y, z);
						if (chunk.GetEntityManager().GetEntityCount() > 0)
							items.push_back({ const_cast<SpatialChunk*>(&chunk), d2 });
					}
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
		 * @brief ワイヤーフレーム用の辺ラインを生成（12エッジ / セル）
		 * @param cp           表示基準点（距離でフェード）
		 * @param outLine      書き込み先（LineList頂点）
		 * @param capacity     outLine の最大頂点数
		 * @param displayCount 表示距離（セル数）
		 * @return 書き込んだ頂点数
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			Math::Vec3f cp,
			float, // dummy
			Debug::LineVertex* outLine,
			uint32_t capacity,
			uint32_t displayCount) const noexcept
		{
			const uint32_t w = grid.width(), h = grid.height(), d = grid.depth();
			const float cell = float(chunkSize);
			const float e = 0.5f * cell;

			const uint32_t kVertsPerCell = 24; // 12本のエッジ × 2頂点
			uint32_t written = 0;
			const float maxLen = displayCount * chunkSize;

			auto push_edge = [&](const Math::Vec3f& a, const Math::Vec3f& b, uint32_t rgb) {
				if (capacity - written < 2) return false;
				outLine[written + 0] = { a, rgb };
				outLine[written + 1] = { b, rgb };
				written += 2;
				return true;
				};

			for (uint32_t z = 0; z < d; ++z) {
				for (uint32_t y = 0; y < h; ++y) {
					for (uint32_t x = 0; x < w; ++x) {
						if (capacity - written < kVertsPerCell) return written; // これ以上書けない

						const Math::Vec3f c{ (x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell };
						const Math::Vec3f ex{ e, e, e };

						// 距離でフェード（XZ距離ではなく3D距離）
						const float len = (c - cp).length();
						if (len > maxLen) continue;

						if (!fr.IntersectsAABB(c, ex)) continue;

						const uint32_t rgb = Math::LerpColor(0xFFFFFFFFu, 0x000000FFu, len / maxLen);

						// 8頂点
						Math::Vec3f v000{ c.x - e, c.y - e, c.z - e };
						Math::Vec3f v001{ c.x - e, c.y - e, c.z + e };
						Math::Vec3f v010{ c.x - e, c.y + e, c.z - e };
						Math::Vec3f v011{ c.x - e, c.y + e, c.z + e };
						Math::Vec3f v100{ c.x + e, c.y - e, c.z - e };
						Math::Vec3f v101{ c.x + e, c.y - e, c.z + e };
						Math::Vec3f v110{ c.x + e, c.y + e, c.z - e };
						Math::Vec3f v111{ c.x + e, c.y + e, c.z + e };

						// 12エッジ
						push_edge(v000, v001, rgb); // -x,-y, z
						push_edge(v000, v010, rgb); // -x, y,-z
						push_edge(v000, v100, rgb); //  x,-y,-z
						push_edge(v111, v101, rgb);
						push_edge(v111, v110, rgb);
						push_edge(v111, v011, rgb);
						push_edge(v010, v011, rgb);
						push_edge(v010, v110, rgb);
						push_edge(v100, v101, rgb);
						push_edge(v100, v110, rgb);
						push_edge(v001, v011, rgb);
						push_edge(v001, v101, rgb);
					}
				}
			}
			return written;
		}

		/**
		 * @brief セルの再ロード（世代を進めて再登録）
		 */
		void ReloadCell(uint32_t cx, uint32_t cy, uint32_t cz, SpatialChunkRegistry& reg)
		{
			SpatialChunk& cell = grid(cx, cy, cz);
			reg.UnregisterOwner(cell.GetNodeKey());

			SpatialChunk newCell = std::move(cell);
			newCell.BumpGeneration();
			reg.RegisterOwner(newCell.GetNodeKey(), &newCell);
			grid(cx, cy, cz) = std::move(newCell);
		}

	private:
		inline SpatialChunkKey MakeGrid3DKey(LevelID level, int32_t gx, int32_t gy, int32_t gz, uint16_t gen = 0)
		{
			SpatialChunkKey k{};
			k.level = level;
			k.scheme = PartitionScheme::Grid3D;
			k.depth = 0;
			k.generation = gen;
			k.code = Morton3D64(ZigZag64(gx), ZigZag64(gy), ZigZag64(gz)); // X,Y,Z の 3Dモートン
			return k;
		}

	private:
		ECS::EntityManager globalEntityManager;                 ///< グローバルEM
		Grid3D<SpatialChunk, ChunkSizeType> grid;               ///< 3Dグリッド
		float chunkSize;                                       ///< セル一辺
		bool isRegistryChunk = false;
	};

	namespace ECS
	{
		/**
		 * @brief Grid3DPartition のチャンク列挙用クエリ（特殊化）
		 */
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(Grid3DPartition& context) const noexcept
		{
			std::vector<ArchetypeChunk*> result;

			auto collect_from = [&](const ECS::EntityManager& em) {
				const auto& all = em.GetArchetypeManager().GetAllData();
				for (const auto& arch : all) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						const auto& chunks = arch->GetChunks();
						result.reserve(result.size() + chunks.size());
						for (const auto& ch : chunks) result.push_back(ch.get());
					}
				}
				};

			// グローバル
			collect_from(context.GetGlobalEntityManager());
			// 空間
			const auto& g = context.GetGrid();
			const uint32_t w = g.width(), h = g.height(), d = g.depth();
			for (uint32_t z = 0; z < d; ++z)
				for (uint32_t y = 0; y < h; ++y)
					for (uint32_t x = 0; x < w; ++x)
						collect_from(g(x, y, z).GetEntityManager());

			return result;
		}
	}
}
