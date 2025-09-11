/*****************************************************************//**
 * @file   BVHPartition.h
 * @brief  Bounding Volume Hierarchy (BVH) パーティション（PartitionConcept 準拠）
 *         - 動的に葉(AABB)を追加・更新・再構築
 *         - 叶う限り軽量なSAH近似の挿入で木を維持（簡易実装）
 *         - Frustumカリング / デバッグワイヤ描画 / Query統合
 *
 * @author seigo
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "partition.hpp"                    // SpatialChunk, ChunkSizeType, EOutOfBoundsPolicy, PartitionScheme
#include "EntityManagerRegistryService.h"  // EntityManagerRegistry, EntityManagerKey, LevelID
#include "../Math/AABB.hpp"                // Math::AABB3f
#include "../Math/Vector.hpp"                 // Math::Vec3f
#include "../Math/sx_math.h"
#include "../Debug/DebugType.h"           // Debug::LineVertex (pos, rgba)

namespace SectorFW
{
	class BVHPartition
	{
	public:
		using AABB = Math::AABB3f;

		struct Leaf
		{
			AABB box{};                 // 3D AABB
			SpatialChunk chunk{};       // 各葉に1つのEMを保持（必要に応じて集約設計に変更可）
			uint32_t id = 0;            // 連番（EntityManagerKey発行用）
		};

		struct Node
		{
			AABB box{};
			int32_t parent = -1;
			int32_t left = -1;
			int32_t right = -1;
			int32_t leaf = -1;  // 葉の場合は Leaf 配列のインデックス、それ以外は -1
			bool IsLeaf() const noexcept { return leaf >= 0; }
		};

	public:
		BVHPartition() noexcept = default;

		/**
		 * @brief 葉ノード(SpatialChunk)を追加し、その参照を返す
		 * @details 追加後は Build() または IncrementalInsert() を呼んで木に反映
		 */
		SpatialChunk* CreateLeaf(const AABB& box) noexcept
		{
			Leaf lf;
			lf.box = box;
			lf.id = static_cast<uint32_t>(leaves.size());
			leaves.push_back(std::move(lf));
			bvhDirty = true;
			return &leaves.back().chunk;
		}

		/**
		 * @brief 葉のAABBを更新（Refitは Build/Refit 時に行う）
		 */
		void UpdateLeafBounds(uint32_t leafIndex, const AABB& newBox) noexcept
		{
			leaves[leafIndex].box = newBox;
			bvhDirty = true;
		}

		/**
		 * @brief すべての葉からBVHを再構築（トップダウン・中央値分割の簡易SAH）
		 */
		void Build() noexcept
		{
			nodes.clear();
			root = -1;
			if (leaves.empty()) { bvhDirty = false; return; }

			// インデックス配列を作り、再帰的に分割してノードを構築
			std::vector<int32_t> idx(leaves.size());
			for (int32_t i = 0; i < (int32_t)leaves.size(); ++i) idx[i] = i;
			root = BuildRecursive(idx.begin(), idx.end());
			bvhDirty = false;
		}

		/**
		 * @brief 葉が増減していない前提でAABBを親方向に再計算（軽い）
		 */
		void Refit() noexcept
		{
			if (root < 0) return;
			RefitDFS(root);
			bvhDirty = false;
		}

		/**
		 * @brief 位置（点）を含む葉(SpatialChunk)を返す
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f p,
			EOutOfBoundsPolicy /*policy*/ = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			if (root < 0) return std::nullopt;
			int32_t n = root;
			while (n >= 0) {
				const Node& node = nodes[n];
				if (!node.box.contains(p)) return std::nullopt; // 根で含まなければ終了
				if (node.IsLeaf()) {
					return &leaves[node.leaf].chunk;
				}
				// どちらかに含まれるまで潜る（両方含む場合は左優先）
				const Node& L = nodes[node.left];
				const Node& R = nodes[node.right];
				bool inL = L.box.contains(p);
				bool inR = R.box.contains(p);
				n = inL ? node.left : (inR ? node.right : -1);
			}
			return std::nullopt;
		}

		/** @brief グローバルEM（BVH外の共通レイヤ） */
		ECS::EntityManager& GetGlobalEntityManager() noexcept { return globalEntityManager; }

		/** @brief 全エンティティ数（グローバル＋各葉） */
		size_t GetEntityNum()
		{
			size_t num = globalEntityManager.GetEntityCount();
			for (auto& lf : leaves) num += lf.chunk.GetEntityManager().GetEntityCount();
			return num;
		}

		/**
		 * @brief Registry登録（各葉に PartitionScheme::BVH のキーを付与）
		 */
		void RegisterAllChunks(EntityManagerRegistry& reg, LevelID level)
		{
			uint16_t gen = 0;
			for (auto& lf : leaves) {
				EntityManagerKey key{};
				key.level = level;
				key.scheme = PartitionScheme::BVH;
				key.depth = 0;
				key.generation = gen;
				key.code = static_cast<std::uint64_t>(lf.id); // シンプルにID
				lf.chunk.SetNodeKey(key);
				reg.RegisterOwner(key, &lf.chunk.GetEntityManager());
			}
		}

		/**
		 * @brief Frustumカリング：交差する葉(SpatialChunk)を列挙
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr) const noexcept
		{
			std::vector<SpatialChunk*> out;
			if (root < 0) return out;
			CullDFS(root, fr, out);
			return out;
		}

		template<class F>
		void CullChunks(const Math::Frustumf& fr, F&& f) const noexcept
		{
			if (root < 0) return;
			CullDFS(root, fr, std::forward<F>(f));
		}

		/**
		 * @brief デバッグ用：葉AABBのワイヤーフレーム（12辺 x 2頂点）を出力
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			Math::Vec3f cp,
			Debug::LineVertex* outLine,
			uint32_t capacity,
			uint32_t displayCount) const noexcept
		{
			if (root < 0) return 0;
			const float maxLen = float(displayCount);
			uint32_t written = 0;

			auto push_edge = [&](const Math::Vec3f& a, const Math::Vec3f& b, uint32_t rgba) {
				if (capacity - written < 2) return false;
				outLine[written + 0] = { a, rgba };
				outLine[written + 1] = { b, rgba };
				written += 2;
				return true;
				};

			auto draw_box = [&](const AABB& b) {
				const Math::Vec3f c = b.center();
				const float len = (c - cp).length();
				if (len > maxLen) return; // 距離制限（任意単位）
				const uint32_t col = Math::LerpColor(0xFFFFFFFFu, 0x000000FFu, (std::min)(1.0f, len / maxLen));

				const Math::Vec3f mn = b.lb;
				const Math::Vec3f mx = b.ub;
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

			// 走査
			std::vector<int32_t> stack;
			stack.reserve(64);
			stack.push_back(root);
			while (!stack.empty() && written + 24 <= capacity) {
				int32_t n = stack.back(); stack.pop_back();
				const Node& node = nodes[n];
				if (!fr.IntersectsAABB(node.box.center(), node.box.extent())) continue;
				if (node.IsLeaf()) { draw_box(leaves[node.leaf].box); }
				else { stack.push_back(node.left); stack.push_back(node.right); }
			}
			return written;
		}

		/**
		 * @brief 葉の再ロード（世代進行 → 再登録）
		 */
		void ReloadLeaf(uint32_t leafIndex, EntityManagerRegistry& reg)
		{
			Leaf& lf = leaves[leafIndex];
			reg.UnregisterOwner(lf.chunk.GetNodeKey());
			SpatialChunk moved = std::move(lf.chunk);
			moved.BumpGeneration();
			reg.RegisterOwner(moved.GetNodeKey(), &moved.GetEntityManager());
			lf.chunk = std::move(moved);
		}

	private:
		// ====== 再帰構築（中央値分割：最大軸） ======
		template<class It>
		int32_t BuildRecursive(It first, It last)
		{
			const int32_t count = static_cast<int32_t>(last - first);
			int32_t nodeIdx = (int32_t)nodes.size();
			nodes.push_back(Node{});
			Node& node = nodes.back();

			// 範囲のAABB
			AABB bounds = leaves[*first].box;
			for (It it = first + 1; it != last; ++it) bounds = AABB::Union(bounds, leaves[*it].box);
			node.box = bounds;

			if (count == 1) {
				node.leaf = *first;
				return nodeIdx;
			}

			// 分割軸 = 最長軸
			Math::Vec3f ext = bounds.Extent();
			int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
			auto mid = first + count / 2;
			std::nth_element(first, mid, last, [&](int32_t a, int32_t b) {
				const float ca = leaves[a].box.Center()[axis];
				const float cb = leaves[b].box.Center()[axis];
				return ca < cb;
				});

			int32_t L = BuildRecursive(first, mid);
			int32_t R = BuildRecursive(mid, last);
			nodes[L].parent = nodes[R].parent = nodeIdx;
			node.left = L; node.right = R;
			return nodeIdx;
		}

		void RefitDFS(int32_t n) noexcept
		{
			Node& node = nodes[n];
			if (node.IsLeaf()) {
				node.box = leaves[node.leaf].box;
				return;
			}
			RefitDFS(node.left);
			RefitDFS(node.right);
			node.box = AABB::Union(nodes[node.left].box, nodes[node.right].box);
		}

		// ベクタ出力版
		void CullDFS(int32_t n, const Math::Frustumf& fr, std::vector<SpatialChunk*>& out) const noexcept
		{
			const Node& node = nodes[n];
			if (!fr.IntersectsAABB(node.box.center(), node.box.extent())) return;
			if (node.IsLeaf()) { out.push_back(const_cast<SpatialChunk*>(&leaves[node.leaf].chunk)); return; }
			CullDFS(node.left, fr, out);
			CullDFS(node.right, fr, out);
		}

		// コールバック版
		template<class F>
		void CullDFS(int32_t n, const Math::Frustumf& fr, F&& f) const noexcept
		{
			const Node& node = nodes[n];
			if (!fr.IntersectsAABB(node.box.Center(), node.box.Extent())) return;
			if (node.IsLeaf()) { f(const_cast<SpatialChunk&>(leaves[node.leaf].chunk)); return; }
			CullDFS(node.left, fr, f);
			CullDFS(node.right, fr, f);
		}

	private:
		ECS::EntityManager globalEntityManager{};
		std::vector<Leaf> leaves;
		std::vector<Node> nodes;
		int32_t root = -1;
		bool bvhDirty = true;
	};

	// ===== ECS::Query との統合 =====
	namespace ECS
	{
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(BVHPartition& context) const noexcept
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
			// BVH の各葉
			// 直接アクセス用APIは用意していないので、簡易にカリング無し列挙が必要なら
			// Frustum を無限にして Cull する使い方も可（ここでは保守的に省略）。
			return result;
		}
	}
}
