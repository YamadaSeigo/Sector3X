/*****************************************************************//**
 * @file   OctreePartition.h
 * @brief 3D 八分木（Octree）による空間分割を定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cassert>

#include "ECS/component.hpp"
#include "partition.hpp"
#include "SpatialChunkRegistryService.h"
#include "../Math/AABB.hpp"
#include "../Math/Frustum.hpp"
#include "../Math/sx_math.h"
#include "../Math/Transform.hpp"

namespace SectorFW
{
	/**
	 * @brief 3D 八分木（Octree）による空間分割
	 * - X, Y, Z の 3 軸で均等分割
	 * - 葉ノードには SpatialChunk（= EntityManager を保持）
	 * - QuadTreePartition と同じ API/挙動を目指して実装
	 *
	 * PartitionConcept は `Derived{ size,size,chunkSize }` を要求するため、
	 * ここでは "size" を立方体の一辺（X=Y=Z）のブロック数として扱い、
	 * minLeafSize (= chunkSize) を 1 ブロックの実寸（ワールド単位）とみなします。
	 */
	class OctreePartition
	{
	public:
		using AABB = Math::AABB3f;

		// 統合を実行する周期（秒）
		static inline constexpr double coalesceInterval = 10.0;
		/**
		 * @brief コンストラクタ
		 * @detail PartitionConceptに合わせるためにworldBlockZを指定しない場合はXと同じになる
		 * @param worldBlocksX X方向のブロックサイズ
		 * @param worldBlocksY Y方向のブロックサイズ
		 * @param minLeafSize 葉チャンク（SpatialChunk）の最小サイズ（ワールド単位）
		 * @param maxEntitiesPerLeaf 葉チャンク（SpatialChunk）あたりの最大エンティティ数
		 * @param worldBlocksZ Z方向のブロックサイズ（省略時はXと同じ）
		 */
		explicit OctreePartition(ChunkSizeType worldBlocksX /*ignored for symmetry*/,
			ChunkSizeType worldBlocksY /*ignored for symmetry*/,
			float minLeafSize,
			ChunkSizeType worldBlocksZ = 0 /*ignored for symmetry*/,
			uint32_t maxEntitiesPerLeaf = 1024) noexcept
			: m_worldX(std::max<ChunkSizeType>(1, ChunkSizeType(worldBlocksX* minLeafSize)))
			, m_worldY(std::max<ChunkSizeType>(1, ChunkSizeType(worldBlocksY* minLeafSize))) // 立方体に合わせる
			, m_worldZ(std::max<ChunkSizeType>(1, ChunkSizeType(worldBlocksZ == 0 ? worldBlocksX : worldBlocksZ * minLeafSize)))
			, m_minLeaf(std::max<float>(1.f, minLeafSize))
			, m_maxPerLeafCount(std::max<uint32_t>(1, maxEntitiesPerLeaf))
		{
			m_root = std::make_unique<Node>();
			m_root->depth = 0;
			m_root->bounds = { Math::Vec3f(0.f, 0.f, 0.f),
							   Math::Vec3f(float(m_worldX), float(m_worldY), float(m_worldZ)) };
			m_leafCount = 1; // 葉として開始
		}

		/**
		 * @brief 定期的に過小利用の下位葉を親へ統合して木を縮約
		 * @param deltaTime 前回呼び出しからの経過時間（秒）
		 */
		void Update(double deltaTime)
		{
			m_coalesceTimer += deltaTime;
			if (m_coalesceTimer >= coalesceInterval) {
				m_coalesceTimer = 0.0;
				CoalesceUnderutilized();
			}
		}
		/**
		 * @brief 点 p を含む葉チャンク（SpatialChunk）を取得
		 * @param p ワールド座標系の点
		 * @param reg チャンクレジストリ
		 * @param level レベルID
		 * @param policy 範囲外ポリシー
		 * @return 点 p を含む葉チャンク（SpatialChunk）。範囲外でポリシーが Reject の場合は std::nullopt
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f p,
			SpatialChunkRegistry& reg, LevelID level,
			EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			if (!inBounds(p.x, p.y, p.z)) {
				if (policy == EOutOfBoundsPolicy::Reject) return std::nullopt;
				p.x = std::clamp(p.x, 0.f, float(m_worldX) - 1e-6f);
				p.y = std::clamp(p.y, 0.f, float(m_worldY) - 1e-6f);
				p.z = std::clamp(p.z, 0.f, float(m_worldZ) - 1e-6f);
			}
			Node* leaf = descendToLeaf(*m_root, p.x, p.y, p.z, /*createIfMissing=*/true);
			EnsureKeyRegisteredForLeaf(*leaf, reg, level);
			return &leaf->chunk;
		}
		/**
		 * @brief 分割に依存しないグローバルな EntityManager を取得
		 * @return グローバルな EntityManager
		 */
		ECS::EntityManager& GetGlobalEntityManager() noexcept { return m_global; }
		/**
		 * @brief すべての葉をレジストリへ登録
		 * @param reg チャンクレジストリ
		 * @param level レベルID
		 */
		void RegisterAllChunks(SpatialChunkRegistry& reg, LevelID level)
		{
			forEachLeaf([&](Node& lf) {
				const auto [ix, iy, iz] = leafIndex(lf);
				SpatialChunkKey key = MakeOctKey(level, lf.depth, ix, iy, iz, /*gen=*/lf.generation);
				lf.chunk.SetNodeKey(key);
				reg.RegisterOwner(key, &lf.chunk);
				});
		}
		/**
		 * @brief すべてのエンティティ数を取得（グローバル + 全葉チャンク）
		 * @return エンティティ数
		 */
		size_t GetEntityNum() const noexcept
		{
			size_t n = m_global.GetEntityCount();
			forEachLeaf([&](const Node& lf) {
				n += lf.chunk.GetEntityManager().GetEntityCount();
				});
			return n;
		}

		/**
		 * @brief 3D フラスタム専用カリング（高さは八分木AABBからそのまま利用
		 * @param fr フラスタム
		 * @return 可視チャンクの配列
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr) noexcept
		{
			std::vector<SpatialChunk*> out; out.reserve(128);
			if (!m_root) return out;
			cullRecursive3D(*m_root, fr, out);
			return out;
		}
		/**
		 * @brief 3D フラスタム専用カリング（高さは八分木AABBからそのまま利用
		 * @param fr フラスタム
		 * @return 可視チャンクの配列
		 */
		std::vector<const SpatialChunk*> CullChunks(const Math::Frustumf& fr) const noexcept
		{
			std::vector<const SpatialChunk*> out; out.reserve(128);
			if (!m_root) return out;
			cullRecursive3D(*m_root, fr, out);
			return out;
		}
		// フラスタムカリングコールバック（高さ補助なし・八分木AABBを直接使用)）
		template<class F>
		void CullChunks(const Math::Frustumf& fr, F&& f) noexcept
		{
			if (!m_root) return;
			cullRecursive3D(*m_root, fr, std::forward<F>(f));
		}
		// フラスタムカリングコールバック（高さ補助なし・八分木AABBを直接使用）
		template<class F>
		void CullChunks(const Math::Frustumf& fr, F&& f) const noexcept
		{
			if (!m_root) return;
			cullRecursive3D(*m_root, fr, std::forward<F>(f));
		}
		// フラスタムカリング（可変高さ）
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr, float ymin, float ymax) noexcept
		{
			std::vector<SpatialChunk*> out; out.reserve(128);
			if (!m_root) return out;
			cullRecursive(*m_root, fr, ymin, ymax, out);
			return out;
		}
		// フラスタムカリング（可変高さ）
		std::vector<const SpatialChunk*> CullChunks(const Math::Frustumf& fr, float ymin, float ymax) const noexcept
		{
			std::vector<const SpatialChunk*> out; out.reserve(128);
			if (!m_root) return out;
			cullRecursive(*m_root, fr, ymin, ymax, out);
			return out;
		}
		template<class F>
		void CullChunks(const Math::Frustumf& fr, float ymin, float ymax, F&& f) noexcept
		{
			if (!m_root) return;
			cullRecursive(*m_root, fr, ymin, ymax, std::forward<F>(f));
		}
		template<class F>
		void CullChunks(const Math::Frustumf& fr, float ymin, float ymax, F&& f) const noexcept
		{
			if (!m_root) return;
			cullRecursive(*m_root, fr, ymin, ymax, std::forward<F>(f));
		}

		/**
		 * @brief チャンクのワイヤーフレームをフラスタムカリング付きで取得
		 * @param fr フラスタム
		 * @param eye 目線位置
		 * @param dummy 未使用（QuadTreePartition と同じシグネチャにするためのダミー）
		 * @param outLine ワイヤーフレーム頂点列の出力先
		 * @param capacity outLine の頂点容量
		 * @param displayCount 最大表示チャンク数
		 * @return ワイヤーフレーム頂点列
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			const Math::Vec3f& eye,
			float, //dummy
			Debug::LineVertex* outLine,
			uint32_t capacity,
			uint32_t displayCount) const noexcept
		{
			if (!m_root || capacity < 24 || displayCount == 0) return 0;

			// 1) 可視ボックス収集（3Dフラスタム判定）
			std::vector<AABB> boxes; boxes.reserve(256);
			cullRecursive3D(*m_root, fr, boxes);
			if (boxes.empty()) return 0;

			// 2) 目線からの距離で前から優先（近い順）
			struct Item { AABB box; float dist; };
			std::vector<Item> items; items.reserve(boxes.size());
			auto clampToBox = [](const AABB& b, const Math::Vec3f& p) {
				return Math::Vec3f{
					std::clamp(p.x, b.lb.x, b.ub.x),
					std::clamp(p.y, b.lb.y, b.ub.y),
					std::clamp(p.z, b.lb.z, b.ub.z)
				};
				};
			for (const auto& b : boxes) {
				const Math::Vec3f q = clampToBox(b, eye);
				const float d = (q - eye).length();
				items.push_back({ b, d });
			}
			std::nth_element(items.begin(), items.begin() + std::min<size_t>(displayCount, items.size()), items.end(),
				[](const Item& a, const Item& b) { return a.dist < b.dist; });
			const size_t useN = std::min<size_t>(displayCount, items.size());

			// 3) 色は距離グラデ（近=白→遠=黒）
			float maxD = 0.f; for (size_t i = 0; i < useN; ++i) maxD = (std::max)(maxD, items[i].dist);
			if (maxD <= 1e-6f) maxD = 1.f;

			auto pushEdge = [&](uint32_t& w, const Math::Vec3f& a, const Math::Vec3f& b, uint32_t rgba) {
				if (w + 2 > capacity) return false;
				outLine[w++] = { a, rgba };
				outLine[w++] = { b, rgba };
				return true;
				};

			uint32_t written = 0;
			for (size_t i = 0; i < useN; ++i) {
				const auto& box = items[i].box;
				const float t = items[i].dist / maxD; // 0..1
				const uint32_t rgba = Math::LerpColor(0xFFFFFFFFu, 0x00000000u, t);

				const Math::Vec3f c = box.center();
				const Math::Vec3f e = box.size() * 0.5f;
				Math::Vec3f v[8]{
					{c.x - e.x, c.y - e.y, c.z - e.z},
					{c.x + e.x, c.y - e.y, c.z - e.z},
					{c.x + e.x, c.y + e.y, c.z - e.z},
					{c.x - e.x, c.y + e.y, c.z - e.z},
					{c.x - e.x, c.y - e.y, c.z + e.z},
					{c.x + e.x, c.y - e.y, c.z + e.z},
					{c.x + e.x, c.y + e.y, c.z + e.z},
					{c.x - e.x, c.y + e.y, c.z + e.z},
				};
				const int edge[12][2] = {
					{0,1},{1,2},{2,3},{3,0},
					{4,5},{5,6},{6,7},{7,4},
					{0,4},{1,5},{2,6},{3,7}
				};
				for (int eidx = 0; eidx < 12; ++eidx) {
					if (!pushEdge(written, v[edge[eidx][0]], v[edge[eidx][1]], rgba))
						return written;
				}
			}
			return written;
		}
		/**
		 * @brief 指定点を含む葉を（必要なら分割しながら）必ず返す
		 * @param p ワールド座標系の点
		 * @return 点 p を含む葉チャンク（SpatialChunk）
		 */
		SpatialChunk* EnsureLeafForPoint(Math::Vec3f p)
		{
			if (!inBounds(p.x, p.y, p.z)) {
				p.x = std::clamp(p.x, 0.f, float(m_worldX) - 1e-6f);
				p.y = std::clamp(p.y, 0.f, float(m_worldY) - 1e-6f);
				p.z = std::clamp(p.z, 0.f, float(m_worldZ) - 1e-6f);
			}
			Node* n = m_root.get();
			while (canSplit(*n)) {
				if (n->isLeaf()) ensureChildren(*n);
				const int oi = octant(*n, p.x, p.y, p.z);
				n = n->child[oi].get();
			}
			return &n->chunk;
		}
		/**
		 * @brief predicate を満たす葉を分割＆再割り当て
		 * @param predicate 分割条件
		 * @param posFn エンティティの位置を取得する関数
		 */
		void SubdivideIf(std::function<bool(const SpatialChunk&)> predicate,
			std::function<Math::Vec3f(ECS::EntityID, ECS::EntityManager&)> posFn)
		{
			std::vector<Node*> targets;
			forEachLeaf([&](Node& lf) {
				if (predicate(lf.chunk) && canSplit(lf)) targets.push_back(&lf);
				});
			for (Node* leaf : targets) subdivideAndReassign(*leaf, posFn);
		}
		/**
		 * @brief 過大利用の葉を分割＆再割り当て
		 * @param posFn エンティティの位置を取得する関数
		 */
		void SubdivideIfOverCapacity(std::function<Math::Vec3f(ECS::EntityID, ECS::EntityManager&)> posFn)
		{
			SubdivideIf([&](const SpatialChunk& sc) {
				return sc.GetEntityManager().GetEntityCount() > m_maxPerLeafCount;
				}, std::move(posFn));
		}
		/**
		 * @brief 指定座標の葉のキーを再発行してレジストリ更新（デバッグ用）
		 * @param p ワールド座標系の点
		 * @param reg チャンクレジストリ
		 * @param level レベルID
		 */
		void ReloadLeafByPoint(Math::Vec3f p, SpatialChunkRegistry& reg, LevelID level)
		{
			auto opt = GetChunk(p, reg, level, EOutOfBoundsPolicy::ClampToEdge);
			if (!opt) return;
			SpatialChunk* leafSC = *opt;

			Node* target = findLeafByChunk(&m_root, leafSC);
			if (!target) return;

			reg.UnregisterOwner(target->chunk.GetNodeKey());

			++target->generation;

			const auto oldKey = target->chunk.GetNodeKey();
			const auto [ix, iy, iz] = leafIndex(*target);
			SpatialChunkKey newKey = MakeOctKey(oldKey.level, target->depth, ix, iy, iz, target->generation);

			target->chunk.SetNodeKey(newKey);
			reg.RegisterOwner(newKey, &target->chunk);
		}
		/**
		 * @brief AABB / 交差検索
		 * @param aabb 検索するAABB
		 * @return 交差するチャンクの配列
		 */
		std::vector<SpatialChunk*> GetChunksAABB(const AABB& aabb)
		{
			std::vector<SpatialChunk*> out; if (!m_root) return out; queryAABB(*m_root, aabb, out); return out;
		}
		/**
		 * @brief AABB / 交差検索
		 * @param aabb 検索するAABB
		 * @return 交差するチャンクの配列
		 */
		std::vector<const SpatialChunk*> GetChunksAABB(const AABB& aabb) const
		{
			std::vector<const SpatialChunk*> out; if (!m_root) return out; queryAABB(*m_root, aabb, out); return out;
		}

		template<class F> void ForEachLeaf(F&& f) { forEachLeaf(std::forward<F>(f)); }
		template<class F> void ForEachLeaf(F&& f) const { forEachLeaf(std::forward<F>(f)); }
		template<class F> void ForEachLeafChunk(F&& f) { forEachLeaf([&](Node& n) { f(n.chunk); }); }
		template<class F> void ForEachLeafChunk(F&& f) const { forEachLeaf([&](const Node& n) { f(n.chunk); }); }
		template<class F> void ForEachLeafEM(F&& f) { forEachLeaf([&](Node& n) { f(n.chunk.GetEntityManager()); }); }
		template<class F> void ForEachLeafEM(F&& f) const { forEachLeaf([&](const Node& n) { f(n.chunk.GetEntityManager()); }); }

		// デバッグ
		uint32_t LeafCount() const noexcept { return m_leafCount; }
		float MinLeafSize() const noexcept { return m_minLeaf; }
		void SetMaxPerLeafCount(uint32_t v) noexcept { m_maxPerLeafCount = v; }
		uint32_t GetMaxPerLeafCount() const noexcept { return m_maxPerLeafCount; }
		void SetMinPerLeafCount(uint32_t v) noexcept { m_minPerLeafCount = v; }
		uint32_t GetMinPerLeafCount() const noexcept { return m_minPerLeafCount; }

	private:
		/**
		 * @brief 葉のノード構造体を定義
		 */
		struct Node {
			AABB bounds{};
			uint16_t generation = 0;
			uint8_t  depth = 0;
			std::array<std::unique_ptr<Node>, 8> child{}; // 0..7 octants
			SpatialChunk chunk; // 葉のみ実質使用

			bool isLeaf() const noexcept {
				for (int i = 0; i < 8; ++i) if (child[i]) return false; return true;
			}
		};

		// ユーティリティ：葉のキーが未発行/未登録なら発行して登録
		inline void EnsureKeyRegisteredForLeaf(Node& leafNode,
			SpatialChunkRegistry& reg,
			LevelID level)
		{
			SpatialChunk& sc = leafNode.chunk;

			// 既に登録済みかを軽く判定：ResolveOwner が取れるか / code==0 等で簡易チェック
			const SpatialChunkKey cur = sc.GetNodeKey();
			if (reg.ResolveOwner(cur) != nullptr && cur.code != 0) return; // 既登録

			const auto [ix, iy, iz] = leafIndex(leafNode);
			SpatialChunkKey key = MakeOctKey(level, leafNode.depth, ix, iy, iz, /*gen*/leafNode.generation);
			sc.SetNodeKey(key);
			reg.RegisterOwner(key, &sc);
		}

		// --- 3D版 AABB 収集 ---
		void cullRecursive3D(const Node& n, const Math::Frustumf& fr, std::vector<AABB>& out) const
		{
			if (!nodeIntersectsFrustum3D(n, fr)) return;
			if (n.isLeaf()) { out.push_back(n.bounds); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive3D(*n.child[i], fr, out);
		}
		// ---- 3D 版カリング（高さ範囲を使わず、八分木の AABB をそのまま使用）----
		bool nodeIntersectsFrustum3D(const Node& n, const Math::Frustumf& fr) const noexcept
		{
			const Math::Vec3f c = (n.bounds.lb + n.bounds.ub) * 0.5f;
			const Math::Vec3f e = (n.bounds.ub - n.bounds.lb) * 0.5f;
			return fr.IntersectsAABB(c, e);
		}
		void cullRecursive3D(Node& n, const Math::Frustumf& fr, std::vector<SpatialChunk*>& out)
		{
			if (!nodeIntersectsFrustum3D(n, fr)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive3D(*n.child[i], fr, out);
		}
		void cullRecursive3D(const Node& n, const Math::Frustumf& fr, std::vector<const SpatialChunk*>& out) const
		{
			if (!nodeIntersectsFrustum3D(n, fr)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive3D(*n.child[i], fr, out);
		}
		template<class F>
		void cullRecursive3D(Node& n, const Math::Frustumf& fr, F&& f)
		{
			if (!nodeIntersectsFrustum3D(n, fr)) return;
			if (n.isLeaf()) { f(n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive3D(*n.child[i], fr, f);
		}
		template<class F>
		void cullRecursive3D(const Node& n, const Math::Frustumf& fr, F&& f) const
		{
			if (!nodeIntersectsFrustum3D(n, fr)) return;
			if (n.isLeaf()) { f(n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive3D(*n.child[i], fr, f);
		}

		// ---- 基本判定 ----
		static bool intersects(const AABB& a, const AABB& b) noexcept {
			return !(a.ub.x <= b.lb.x || a.lb.x >= b.ub.x ||
				a.ub.y <= b.lb.y || a.lb.y >= b.ub.y ||
				a.ub.z <= b.lb.z || a.lb.z >= b.ub.z);
		}
		bool inBounds(float x, float y, float z) const noexcept {
			return (0.f <= x && x < float(m_worldX)) &&
				(0.f <= y && y < float(m_worldY)) &&
				(0.f <= z && z < float(m_worldZ));
		}
		bool canSplit(const Node& n) const noexcept {
			const Math::Vec3f s = n.bounds.size();
			return (s.x > m_minLeaf) && (s.y > m_minLeaf) && (s.z > m_minLeaf);
		}

		int octant(const Node& n, float x, float y, float z) const noexcept {
			const float mx = 0.5f * (n.bounds.lb.x + n.bounds.ub.x);
			const float my = 0.5f * (n.bounds.lb.y + n.bounds.ub.y);
			const float mz = 0.5f * (n.bounds.lb.z + n.bounds.ub.z);
			const int ex = (x >= mx) ? 1 : 0; // East(1)/West(0)
			const int uy = (y >= my) ? 1 : 0; // Up(1)/Down(0)
			const int nz = (z >= mz) ? 1 : 0; // North(1)/South(0)
			// ビット: x(1) | y(2) | z(4)
			return (ex) | (uy << 1) | (nz << 2); // 0..7
		}

		void ensureChildren(Node& n)
		{
			if (!n.isLeaf()) return;
			const float mx = 0.5f * (n.bounds.lb.x + n.bounds.ub.x);
			const float my = 0.5f * (n.bounds.lb.y + n.bounds.ub.y);
			const float mz = 0.5f * (n.bounds.lb.z + n.bounds.ub.z);

			const AABB octs[8] = {
				// x:[lb..mx], y:[lb..my], z:[lb..mz]
				{ { n.bounds.lb.x, n.bounds.lb.y, n.bounds.lb.z }, { mx,            my,            mz } }, // 0: W-D-S
				{ { mx,            n.bounds.lb.y, n.bounds.lb.z }, { n.bounds.ub.x, my,            mz } }, // 1: E-D-S
				{ { n.bounds.lb.x, my,            n.bounds.lb.z }, { mx,            n.bounds.ub.y, mz } }, // 2: W-U-S
				{ { mx,            my,            n.bounds.lb.z }, { n.bounds.ub.x, n.bounds.ub.y, mz } }, // 3: E-U-S
				{ { n.bounds.lb.x, n.bounds.lb.y, mz            }, { mx,            my,            n.bounds.ub.z } }, // 4: W-D-N
				{ { mx,            n.bounds.lb.y, mz            }, { n.bounds.ub.x, my,            n.bounds.ub.z } }, // 5: E-D-N
				{ { n.bounds.lb.x, my,            mz            }, { mx,            n.bounds.ub.y, n.bounds.ub.z } }, // 6: W-U-N
				{ { mx,            my,            mz            }, { n.bounds.ub.x, n.bounds.ub.y, n.bounds.ub.z } }  // 7: E-U-N
			};
			for (int i = 0; i < 8; ++i) {
				n.child[i] = std::make_unique<Node>();
				n.child[i]->bounds = octs[i];
				n.child[i]->depth = uint8_t(n.depth + 1);
				n.child[i]->generation = 0;
			}
			m_leafCount += 7; // 葉1 → 子8
		}

		Node* descendToLeaf(Node& cur, float x, float y, float z, bool createIfMissing)
		{
			auto posFn = [](ECS::EntityID id, ECS::EntityManager& mgr) -> std::optional<Math::Vec3f> {
				auto tf = mgr.GetComponent<CTransform>(id);
				if (tf == nullptr) return std::nullopt;
				return tf->location;
				};

			Node* n = &cur;
			while (canSplit(*n)) {
				if (n->isLeaf()) {
					if (!createIfMissing) break;
					if (n->chunk.GetEntityManager().GetEntityCount() > 0) {
						subdivideAndReassign(*n, posFn); // 中身あり -> 分割して再配置
					}
					else {
						ensureChildren(*n); // 空 -> 子生成のみ
					}
				}
				const int oi = octant(*n, x, y, z);
				n = n->child[oi].get();
			}
			return n;
		}

		// ---- クエリ ----
		void queryAABB(Node& n, const AABB& q, std::vector<SpatialChunk*>& out)
		{
			if (!intersects(n.bounds, q)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) queryAABB(*n.child[i], q, out);
		}
		void queryAABB(const Node& n, const AABB& q, std::vector<const SpatialChunk*>& out) const
		{
			if (!intersects(n.bounds, q)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) queryAABB(*n.child[i], q, out);
		}

		std::tuple<uint32_t, uint32_t, uint32_t> leafIndex(const Node& n) const noexcept
		{
			const float base = (std::max)(1.0f, m_minLeaf);
			const float scale = 1.0f / base;
			const Math::Vec3f lb = n.bounds.lb;
			const uint32_t ix = static_cast<uint32_t>(std::floor(lb.x * scale));
			const uint32_t iy = static_cast<uint32_t>(std::floor(lb.y * scale));
			const uint32_t iz = static_cast<uint32_t>(std::floor(lb.z * scale));
			return { ix, iy, iz };
		}

		Node* findLeafByChunk(std::unique_ptr<Node>* pp, SpatialChunk* sc) const
		{
			if (!pp || !pp->get()) return nullptr;
			Node* n = pp->get();
			if (n->isLeaf()) return (&n->chunk == sc) ? n : nullptr;
			for (int i = 0; i < 8; ++i) if (Node* r = findLeafByChunk(&n->child[i], sc)) return r;
			return nullptr;
		}

		// フラスタムとの交差（Y 範囲を事前にフラスタムへ射影するユーティリティを使用）
		bool nodeIntersectsFrustum(const Node& n, const Math::Frustumf& fr,
			float ymin, float ymax) const noexcept
		{
			const Math::Vec3f c = (n.bounds.lb + n.bounds.ub) * 0.5f;
			const Math::Vec3f e = (n.bounds.ub - n.bounds.lb) * 0.5f;

			float cyEff, eyEff;
			if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, c.x, c.z, ymin, ymax, cyEff, eyEff))
				return false; // 縦に重ならない
			const Math::Vec3f center{ c.x, cyEff, c.z };
			const Math::Vec3f extent{ e.x, eyEff, e.z };
			return fr.IntersectsAABB(center, extent);
		}

		// ---- カリング ----
		void cullRecursive(Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<SpatialChunk*>& out)
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}
		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<const SpatialChunk*>& out) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { out.push_back(&n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}
		template<class F>
		void cullRecursive(Node& n, const Math::Frustumf& fr, float ymin, float ymax, F&& f)
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { f(n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, f);
		}
		template<class F>
		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax, F&& f) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { f(n.chunk); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, f);
		}
		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<AABB>& out) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { out.push_back(n.bounds); return; }
			for (int i = 0; i < 8; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}

		// ---- 葉列挙 ----
		template<class F>
		void forEachLeaf(F&& f)
		{
			if (!m_root) return;
			std::vector<Node*> stack; stack.reserve(128);
			stack.push_back(m_root.get());
			while (!stack.empty()) {
				Node* n = stack.back(); stack.pop_back();
				if (n->isLeaf()) { f(*n); continue; }
				for (int i = 0; i < 8; ++i) if (n->child[i]) stack.push_back(n->child[i].get());
			}
		}
		template<class F>
		void forEachLeaf(F&& f) const
		{
			if (!m_root) return;
			std::vector<const Node*> stack; stack.reserve(128);
			stack.push_back(m_root.get());
			while (!stack.empty()) {
				const Node* n = stack.back(); stack.pop_back();
				if (n->isLeaf()) { f(*n); continue; }
				for (int i = 0; i < 8; ++i) if (n->child[i]) stack.push_back(n->child[i].get());
			}
		}

		// ---- 分割と再配置 ----
		void subdivideAndReassign(Node& leaf,
			const std::function<std::optional<Math::Vec3f>(ECS::EntityID, ECS::EntityManager&)>& posFn)
		{
			if (!leaf.isLeaf() || !canSplit(leaf)) return;

			ensureChildren(leaf);
			ECS::EntityManager& src = leaf.chunk.GetEntityManager();

			auto router = [&](ECS::EntityID id, const ECS::ComponentMask /*mask*/) -> ECS::EntityManager* {
				const std::optional<Math::Vec3f> pos = posFn(id, src);
				if (!pos) return nullptr;
				const int oi = octant(leaf, pos->x, pos->y, pos->z);
				return &leaf.child[oi]->chunk.GetEntityManager();
				};
			(void)src.SplitByAll(router); // 非スパース→逐次, スパース→IDバケット一括
			++leaf.generation;
		}

		/**
		 * @brief 4 子が葉の親を統合して葉へ戻す（Octree では 8 子）
		 * @return マージした親ノード数
		 */
		size_t CoalesceUnderutilized()
		{
			if (!m_root) return 0;
			std::vector<Node*> stack; stack.push_back(m_root.get());
			std::vector<Node*> post;
			while (!stack.empty()) {
				Node* n = stack.back(); stack.pop_back();
				post.push_back(n);
				for (auto& c : n->child) if (c) stack.push_back(c.get());
			}
			size_t mergedParents = 0;

			for (auto it = post.rbegin(); it != post.rend(); ++it) {
				Node* n = *it;
				bool allLeaf = true; for (int i = 0; i < 8; ++i) { if (!n->child[i] || !n->child[i]->isLeaf()) { allLeaf = false; break; } }
				if (!allLeaf) continue;

				size_t sum = 0; for (int i = 0; i < 8; ++i) sum += n->child[i]->chunk.GetEntityManager().GetEntityCount();
				if (sum <= m_minPerLeafCount) {
					auto& dst = n->chunk.GetEntityManager();
					for (int i = 0; i < 8; ++i) {
						auto& src = n->child[i]->chunk.GetEntityManager();
						(void)dst.MergeFromAll(src);
					}
					n->child = {};
					++n->generation;
					m_leafCount -= 7; // 8 -> 1
					++mergedParents;
				}
			}
			return mergedParents;
		}

		// ---- キー生成（Morton3D 64bit 符号化） ----
		static inline uint64_t ExpandBits3(uint64_t v)
		{
			v = (v | (v << 32)) & 0x1F00000000FFFFull;
			v = (v | (v << 16)) & 0x1F0000FF0000FFull;
			v = (v | (v << 8)) & 0x100F00F00F00F00Full;
			v = (v | (v << 4)) & 0x10C30C30C30C30C3ull;
			v = (v | (v << 2)) & 0x1249249249249249ull;
			return v;
		}
		static inline uint64_t Morton3D64(uint64_t x, uint64_t y, uint64_t z)
		{
			return (ExpandBits3(x) << 0) | (ExpandBits3(y) << 1) | (ExpandBits3(z) << 2);
		}

		inline SpatialChunkKey MakeOctKey(LevelID level, uint8_t depth,
			uint32_t ix, uint32_t iy, uint32_t iz,
			uint16_t gen = 0)
		{
			SpatialChunkKey k{};
			k.level = level;
			k.scheme = PartitionScheme::Octree3D; // ※ 定義が無い場合は追加/変更してください
			k.depth = depth;
			k.generation = gen;
			const uint64_t morton = Morton3D64(ix, iy, iz);
			k.code = (uint64_t(depth) << 56) | (morton & 0x00FF'FFFF'FFFF'FFFFull);
			return k;
		}

	private:
		ECS::EntityManager m_global;
		std::unique_ptr<Node> m_root;

		ChunkSizeType m_worldX = 0;
		ChunkSizeType m_worldY = 0;
		ChunkSizeType m_worldZ = 0;
		float m_minLeaf = 1.0f;

		uint32_t m_minPerLeafCount = 0;  // 統合トリガ（子合計がこれ以下なら親へ集約）
		uint32_t m_maxPerLeafCount = 1024;

		uint32_t m_leafCount = 1;
		double m_coalesceTimer = 0.0;
	};

	// ==== Query 特殊化 ====
	namespace ECS {
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(SectorFW::OctreePartition& ctx) const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			result.reserve(ctx.LeafCount());

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

			collect_from(ctx.GetGlobalEntityManager());
			ctx.ForEachLeafEM([&](const ECS::EntityManager& em) { collect_from(em); });
			return result;
		}
	} // namespace ECS
} // namespace SectorFW
