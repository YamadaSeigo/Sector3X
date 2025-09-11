/*****************************************************************//**
 * @file   SAPPartition.h
 * @brief  Sweep And Prune (SAP) ブロードフェーズ用パーティション（PartitionConcept 準拠）
 *         - 3軸の投影区間(min/max)を保持し、必要に応じて再ソートして高速に広域判定
 *         - Frustumカリング / デバッグワイヤ描画 / ECS::Query 統合
 *         - Grid/Octree/BVH と併用できるよう軽量設計
 *
 * @author seigo
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "partition.hpp"                    // SpatialChunk, ChunkSizeType, PartitionScheme
#include "EntityManagerRegistryService.h"  // EntityManagerRegistry, EntityManagerKey, LevelID
#include "../Math/AABB.hpp"                // Math::AABB3f
#include "../Math/Vector.hpp"                 // Math::Vec3f
#include "../Math/sx_math.h"
#include "../Debug/DebugType.h"           // Debug::LineVertex

namespace SectorFW
{
	class SAPPartition
	{
	public:
		using AABB = Math::AABB3f;

		struct Body
		{
			AABB box{};                 // ワールド空間AABB
			SpatialChunk chunk{};       // 所属EM（1ボディ=1チャンク想定）
			uint32_t id = 0;            // 連番ID
			// 投影区間（再構築時に更新）
			float minProj[3]{};
			float maxProj[3]{};
		};

		// 軸指定
		enum class Axis : uint8_t { X = 0, Y = 1, Z = 2 };

	public:
		explicit SAPPartition(Axis primaryAxis = Axis::X) noexcept
			: primary(primaryAxis) {
		}

		/**
		 * @brief ボディ（=SpatialChunk）を追加
		 */
		SpatialChunk* CreateBody(const AABB& box) noexcept
		{
			Body b{};
			b.box = box;
			b.id = static_cast<uint32_t>(bodies.size());
			UpdateProj(b);
			bodies.push_back(std::move(b));
			markDirty();
			return &bodies.back().chunk;
		}

		/**
		 * @brief AABB更新（移動体など）
		 */
		void UpdateBodyBounds(uint32_t bodyIndex, const AABB& newBox) noexcept
		{
			Body& b = bodies[bodyIndex];
			b.box = newBox;
			UpdateProj(b);
			markDirty();
		}

		/**
		 * @brief すべての投影配列を再構築（安定ソート）
		 *        頻繁更新ならフレーム終端で呼ぶ想定
		 */
		void Rebuild() noexcept
		{
			buildIndexForAxis(0, orderX);
			buildIndexForAxis(1, orderY);
			buildIndexForAxis(2, orderZ);
			dirty = false;
		}

		/**
		 * @brief 点が含まれるボディのチャンクを返す（最初の一致を返却）
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f p,
			EOutOfBoundsPolicy /*policy*/ = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			ensureRebuilt();
			for (auto idx : orderPrimary()) {
				Body& b = bodies[idx];
				if (b.box.contains(p)) return &b.chunk;
			}
			return std::nullopt;
		}

		/** @brief グローバルEM */
		ECS::EntityManager& GetGlobalEntityManager() noexcept { return globalEntityManager; }

		/** @brief 全エンティティ数（グローバル＋各ボディ） */
		size_t GetEntityNum()
		{
			size_t num = globalEntityManager.GetEntityCount();
			for (auto& b : bodies) num += b.chunk.GetEntityManager().GetEntityCount();
			return num;
		}

		/**
		 * @brief Registry登録（PartitionScheme::SAP）
		 */
		void RegisterAllChunks(EntityManagerRegistry& reg, LevelID level)
		{
			uint16_t gen = 0;
			for (auto& b : bodies) {
				EntityManagerKey key{};
				key.level = level;
				key.scheme = PartitionScheme::SAP;
				key.depth = 0;
				key.generation = gen;
				key.code = static_cast<std::uint64_t>(b.id);
				b.chunk.SetNodeKey(key);
				reg.RegisterOwner(key, &b.chunk.GetEntityManager());
			}
		}

		/**
		 * @brief Frustumカリング：交差ボディを列挙
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr) const noexcept
		{
			std::vector<SpatialChunk*> out;
			for (const auto& b : bodies) {
				if (fr.IntersectsAABB(b.box.center(), b.box.extent()))
					out.push_back(const_cast<SpatialChunk*>(&b.chunk));
			}
			return out;
		}

		template<class F>
		void CullChunks(const Math::Frustumf& fr, F&& f) const noexcept
		{
			for (const auto& b : bodies) {
				if (fr.IntersectsAABB(b.box.Center(), b.box.Extent()))
					f(const_cast<SpatialChunk&>(b.chunk));
			}
		}

		/**
		 * @brief デバッグ：各ボディAABBのワイヤ（12エッジ）
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			Math::Vec3f cp,
			Debug::LineVertex* outLine,
			uint32_t capacity,
			uint32_t displayCount) const noexcept
		{
			const float maxLen = float(displayCount);
			uint32_t written = 0;

			auto push_edge = [&](const Math::Vec3f& a, const Math::Vec3f& b, uint32_t rgba) {
				if (capacity - written < 2) return false;
				outLine[written + 0] = { a, rgba };
				outLine[written + 1] = { b, rgba };
				written += 2;
				return true;
				};

			auto draw_box = [&](const AABB& box) {
				const Math::Vec3f c = box.center();
				const float len = (c - cp).length();
				if (len > maxLen) return; // 距離制限
				const uint32_t col = Math::LerpColor(0xFFFFFFFFu, 0x000000FFu, (std::min)(1.0f, len / maxLen));

				const Math::Vec3f mn = box.lb, mx = box.ub;
				Math::Vec3f v000{ mn.x, mn.y, mn.z };
				Math::Vec3f v001{ mn.x, mn.y, mx.z };
				Math::Vec3f v010{ mn.x, mx.y, mn.z };
				Math::Vec3f v011{ mn.x, mx.y, mx.z };
				Math::Vec3f v100{ mx.x, mn.y, mn.z };
				Math::Vec3f v101{ mx.x, mn.y, mx.z };
				Math::Vec3f v110{ mx.x, mx.y, mn.z };
				Math::Vec3f v111{ mx.x, mx.y, mx.z };

				push_edge(v000, v001, col); push_edge(v000, v010, col); push_edge(v000, v100, col);
				push_edge(v111, v101, col); push_edge(v111, v110, col); push_edge(v111, v011, col);
				push_edge(v010, v011, col); push_edge(v010, v110, col); push_edge(v100, v101, col);
				push_edge(v100, v110, col); push_edge(v001, v011, col); push_edge(v001, v101, col);
				};

			for (const auto& b : bodies) {
				if (!fr.IntersectsAABB(b.box.center(), b.box.extent())) continue;
				if (written + 24 > capacity) break;
				draw_box(b.box);
			}
			return written;
		}

		/**
		 * @brief 近傍候補（重なり）ペア列挙（ブロードフェーズ）
		 * @details primary軸のソートを使い、min<=curr.max の範囲でスイープ。
		 *          2軸/3軸目は早期判定用のAABBチェックで絞り込み。
		 */
		template<class PairFn>
		void EnumerateOverlapPairs(PairFn&& fn) noexcept
		{
			ensureRebuilt();
			const auto& order = orderPrimary();
			// アクティブリスト方式
			std::vector<uint32_t> active;
			active.reserve(128);
			for (uint32_t i = 0; i < order.size(); ++i) {
				const uint32_t aIdx = order[i];
				Body& A = bodies[aIdx];
				const float aMin = A.minProj[(int)primary];
				const float aMax = A.maxProj[(int)primary];

				// active から、終端がAの始端よりも小さいものを除去
				auto it = active.begin();
				while (it != active.end()) {
					if (bodies[*it].maxProj[(int)primary] < aMin) it = active.erase(it);
					else ++it;
				}

				// active と交差候補
				for (uint32_t j : active) {
					Body& B = bodies[j];
					if (A.box.Overlaps(B.box)) {
						fn(aIdx, j); // A と B は重なり候補
					}
				}

				active.push_back(aIdx);
			}
		}

		/**
		 * @brief ボディ再ロード
		 */
		void ReloadBody(uint32_t bodyIndex, EntityManagerRegistry& reg)
		{
			Body& b = bodies[bodyIndex];
			reg.UnregisterOwner(b.chunk.GetNodeKey());
			SpatialChunk moved = std::move(b.chunk);
			moved.BumpGeneration();
			reg.RegisterOwner(moved.GetNodeKey(), &moved.GetEntityManager());
			b.chunk = std::move(moved);
		}

	private:
		void UpdateProj(Body& b) noexcept
		{
			b.minProj[0] = b.box.lb.x; b.maxProj[0] = b.box.ub.x;
			b.minProj[1] = b.box.lb.y; b.maxProj[1] = b.box.ub.y;
			b.minProj[2] = b.box.lb.z; b.maxProj[2] = b.box.ub.z;
		}

		void buildIndexForAxis(int axis, std::vector<uint32_t>& out) noexcept
		{
			out.resize(bodies.size());
			for (uint32_t i = 0; i < bodies.size(); ++i) out[i] = i;
			std::stable_sort(out.begin(), out.end(), [&](uint32_t a, uint32_t b) {
				return bodies[a].minProj[axis] < bodies[b].minProj[axis];
				});
		}

		void markDirty() noexcept { dirty = true; }
		void ensureRebuilt() noexcept { if (dirty) Rebuild(); }

		const std::vector<uint32_t>& orderPrimary() const noexcept
		{
			switch (primary) {
			case Axis::X: return orderX;
			case Axis::Y: return orderY;
			default:      return orderZ;
			}
		}

	private:
		ECS::EntityManager globalEntityManager{};
		std::vector<Body> bodies;
		std::vector<uint32_t> orderX, orderY, orderZ;
		Axis primary = Axis::X;
		bool dirty = true;
	};

	// ===== ECS::Query 統合 =====
	namespace ECS
	{
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(SAPPartition& context) const noexcept
		{
			std::vector<ArchetypeChunk*> result;

			auto collect_from = [&](const ECS::EntityManager& em) {
				const auto& all = em.GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : all) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						const auto& chunks = arch->GetChunks();
						result.reserve(result.size() + chunks.size());
						for (const auto& ch : chunks) result.push_back(ch.get());
					}
				}
				};

			collect_from(context.GetGlobalEntityManager());
			// SAP内の個々のボディEMは、必要に応じて別途カリング経由で列挙してください。
			return result;
		}
	}
}
