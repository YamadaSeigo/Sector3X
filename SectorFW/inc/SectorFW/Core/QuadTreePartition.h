/*****************************************************************//**
 * @file   QuadTreePartition.h
 * @brief クアッドツリーパーティションを定義するクラス
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
#include "../Util/Morton.h"
#include "../Math/AABB.hpp"

namespace SFW
{
	/**
	 * @brief クアッドツリー(x-z)パーティションを管理するクラス
	 */
	class QuadTreePartition
	{
	public:
		using AABB = Math::AABB2f;
		struct Circle { float cx, cy, r; };

		enum class ZPositive { North, South };
		//========================================================================
		// Z+が北か南か
		static constexpr ZPositive kZPositive = ZPositive::North;
		//========================================================================

		//統合するかチェック間隔
		static inline constexpr double coalesceInterval = 10.0; // 秒
		/**
		 * @brief コンストラクタ
		 * @param worldW X方向の最小葉の数
		 * @param worldH Z方向の最小葉の数
		 * @param minLeafSize 最小葉のサイズ(ワールドサイズに対する比率、0.0~1.0)
		 * @param maxEntitiesPerLeaf 1つの葉に格納できるエンティティの最大数
		 */
		explicit QuadTreePartition(ChunkSizeType worldW,
			ChunkSizeType worldH,
			float minLeafSize,
			uint32_t maxEntitiesPerLeaf = 1024) noexcept
			: m_worldW(std::max<ChunkSizeType>(1, ChunkSizeType(worldW* minLeafSize)))
			, m_worldH(std::max<ChunkSizeType>(1, ChunkSizeType(worldH* minLeafSize)))
			, m_minLeaf(std::max<float>(1.f, minLeafSize))
			, m_maxPerLeafCount(std::max<uint32_t>(1, maxEntitiesPerLeaf))
		{
			m_root = std::make_unique<Node>();
			m_root->depth = 0;
			m_root->bounds = { Math::Vec2f(0.f, 0.f), Math::Vec2f(float(m_worldW), float(m_worldH)) };
			m_leafCount = 1; // 葉としてスタート
		}
		/**
		 * @brief エンティティの数が少ない葉を統合ために更新
		 * @param deltaTime 前回の更新からの経過時間(秒)
		 */
		void Update(double deltaTime) {
			m_coalesceTimer += deltaTime;
			if (m_coalesceTimer >= coalesceInterval) {
				m_coalesceTimer = 0.0;
				CoalesceUnderutilized();
			}
		}
		/**
		 * @brief 指定した位置にあるチャンクを取得する関数
		 * @param p 位置(x,z)
		 * @param reg チャンクレジストリ
		 * @param level レベルID
		 * @param policy 範囲外ポリシー
		 * @return チャンクへのポインタ(存在しない場合はstd::nullopt)
		 */
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f p,
			SpatialChunkRegistry& reg, LevelID level,
			EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			if (!inBounds(p.x, p.z)) {
				if (policy == EOutOfBoundsPolicy::Reject) return std::nullopt;
				// Clamp to [0, W/H)
				p.x = std::clamp(p.x, 0.f, float(m_worldW) - 1e-6f);
				p.z = std::clamp(p.z, 0.f, float(m_worldH) - 1e-6f);
			}
			Node* leaf = descendToLeaf(*m_root, p.x, p.z, /*createIfMissing=*/true);
			EnsureKeyRegisteredForLeaf(*leaf, reg, level);
			return &leaf->chunk;
		}
		/**
		 * @brief グローバルエンティティマネージャーを取得する関数
		 * @return グローバルエンティティマネージャーへの参照
		 */
		ECS::EntityManager& GetGlobalEntityManager() noexcept { return m_global; }
		/**
		 * @brief すべてのチャンクをレジストリに登録する関数
		 * @param reg チャンクレジストリ
		 * @param level レベルID
		 */
		void RegisterAllChunks(SpatialChunkRegistry& reg, LevelID level)
		{
			forEachLeaf([&](Node& lf) {
				const auto [ix, iy] = leafIndex(lf);
				SpatialChunkKey key = MakeQuadKey(level, lf.depth, ix, iy, /*gen=*/lf.generation);
				lf.chunk.SetNodeKey(key);
				reg.RegisterOwner(key, &lf.chunk);
				});
		}
		/**
		 * @brief すべてのエンティティ数を取得する関数
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
		 * @brief 指定した視錐台に含まれるチャンクを列挙する関数
		 * @param fr 視錐台
		 * @param ymin 最低Y座標(省略時は無制限)
		 * @param ymax 最高Y座標(省略時は無制限)
		 * @return チャンクのポインタ配列
		 */
		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf& fr,
			float ymin = std::numeric_limits<float>::lowest(), float ymax = (std::numeric_limits<float>::max)()) noexcept
		{
			std::vector<SpatialChunk*> out;
			out.reserve(64);
			if (!m_root) return out;
			cullRecursive(*m_root, fr, ymin, ymax, out);
			return out;
		}

		std::vector<const SpatialChunk*> CullChunks(const Math::Frustumf& fr,
			float ymin = std::numeric_limits<float>::lowest(), float ymax = (std::numeric_limits<float>::max)()) const noexcept
		{
			std::vector<const SpatialChunk*> out;
			out.reserve(64);
			if (!m_root) return out;
			cullRecursive(*m_root, fr, ymin, ymax, out);
			return out;
		}

		static inline float Dist2PointAABB3D(const Math::Vec3f& p,
			const Math::Vec2f& c,
			const Math::Vec2f& e)
		{
			auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
			const float qx = clamp(p.x, c.x - e.x, c.x + e.x);
			const float qz = clamp(p.z, c.y - e.y, c.y + e.y);
			const float dx = p.x - qx, dz = p.z - qz;
			return dx * dx + dz * dz;
		}

		std::vector<SpatialChunk*> CullChunksNear(const Math::Frustumf& fr,
			const Math::Vec3f& camPos,
			size_t maxCount = (std::numeric_limits<size_t>::max)(),
			float ymin = std::numeric_limits<float>::lowest(),
			float ymax = (std::numeric_limits<float>::max)()) const noexcept
		{
			struct Item { SpatialChunk* sc; float d2; };

			std::vector<Item> items; items.reserve(128);
			if (!m_root) return {};

			// 可視葉 (chunk, bounds) を同時に収集する内部再帰
			std::function<void(const Node&)> rec = [&](const Node& n) {
				if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
				if (n.isLeaf()) {
					const Math::Vec2f c = (n.bounds.lb + n.bounds.ub) * 0.5f;
					const Math::Vec2f e = (n.bounds.ub - n.bounds.lb) * 0.5f;
					const float d2 = 10.0f;//this->Dist2PointAABB3D(camPos, c, e);
					if (n.chunk.GetEntityManager().GetEntityCount() > 0)
						items.push_back({ const_cast<SpatialChunk*>(&n.chunk), d2 });
					return;
				}
				for (int i = 0; i < 4; ++i) if (n.child[i]) rec(*n.child[i]);
				};
			rec(*m_root);

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
		 * @brief チャンクの境界線をデバッグ描画する関数
		 * @param fr 視錐台
		 * @param cp カメラ位置
		 * @param hy カメラ高さからの上下の伸び
		 * @param outLine 出力先のラインバッファ
		 * @param capacity 出力先のラインバッファの容量(頂点数)
		 * @param displayCount 表示するチャンクの最大数(0の場合は表示しない)
		 * @return 書き込んだ頂点数
		 */
		uint32_t CullChunkLine(const Math::Frustumf& fr,
			Math::Vec3f cp, float hy, Debug::LineVertex* outLine,
			uint32_t capacity, uint32_t displayCount) const noexcept
		{
			if (!m_root || capacity < 6 || displayCount == 0) return 0;

			std::vector<AABB> boxes;
			boxes.reserve(64);

			cullRecursive(*m_root, fr, cp.y - hy, cp.y + hy, boxes);

			struct Item { AABB box; float dist; };
			std::vector<Item> items; items.reserve(boxes.size());
			auto clampToBox = [](const AABB& b, const Math::Vec3f& p) {
				return Math::Vec2f{
					std::clamp(p.x, b.lb.x, b.ub.x),
					std::clamp(p.z, b.lb.y, b.ub.y)
				};
				};
			for (const auto& b : boxes) {
				const Math::Vec2f q = clampToBox(b, cp);
				Math::Vec2f vec{ q.x - cp.x, q.y - cp.z };
				const float d = vec.length();
				items.push_back({ b, d });
			}
			std::nth_element(items.begin(), items.begin() + std::min<size_t>(displayCount, items.size()), items.end(),
				[](const Item& a, const Item& b) { return a.dist < b.dist; });
			const size_t useN = std::min<size_t>(displayCount, items.size());

			// 3) 色は距離グラデ（近=白→遠=黒）
			float maxD = 0.f; for (size_t i = 0; i < useN; ++i) maxD = (std::max)(maxD, items[i].dist);
			if (maxD <= 1e-6f) maxD = 1.f;

			uint32_t written = 0;
			for (const auto& box : boxes) {
				const Math::Vec2f center = box.center();
				const Math::Vec2f extent = box.size() * 0.5f;

				const Math::Vec2f vec{ center.x - cp.x, center.y - cp.z };
				const float len = vec.length();

				if (len > maxD) continue; // 表示距離外

				if (capacity - written < 8) break; // 必須：容量チェック

				const uint32_t rgba = Math::LerpColor(0xFFFFFFFFu, 0x00000000u, len / maxD);

				outLine[written + 0] = { Math::Vec3f(center.x - extent.x, cp.y - hy, center.y - extent.y), rgba };
				outLine[written + 1] = { Math::Vec3f(center.x - extent.x, cp.y + hy, center.y - extent.y), rgba };
				outLine[written + 2] = { Math::Vec3f(center.x + extent.x, cp.y - hy, center.y - extent.y), rgba };
				outLine[written + 3] = { Math::Vec3f(center.x + extent.x, cp.y + hy, center.y - extent.y), rgba };
				outLine[written + 4] = { Math::Vec3f(center.x - extent.x, cp.y - hy, center.y + extent.y), rgba };
				outLine[written + 5] = { Math::Vec3f(center.x - extent.x, cp.y + hy, center.y + extent.y), rgba };
				outLine[written + 6] = { Math::Vec3f(center.x + extent.x, cp.y - hy, center.y + extent.y), rgba };
				outLine[written + 7] = { Math::Vec3f(center.x + extent.x, cp.y + hy, center.y + extent.y), rgba };

				written += 8;
			}
			return written;
		}

		SpatialChunk* EnsureLeafForPoint(Math::Vec3f p)
		{
			if (!inBounds(p.x, p.z)) {
				p.x = std::clamp(p.x, 0.f, float(m_worldW) - 1e-6f);
				p.z = std::clamp(p.z, 0.f, float(m_worldH) - 1e-6f);
			}
			Node* n = m_root.get();
			while (canSplit(*n)) {
				if (n->isLeaf()) ensureChildren(*n);   // 葉のとき子を作る
				const int qi = quadrant(*n, p.x, p.z);  //xz 統一
				n = n->child[qi].get();
			}
			return &n->chunk;
		}

		// 葉を条件で分割して再配置
		void SubdivideIf(std::function<bool(const SpatialChunk&)> predicate,
			std::function<Math::Vec3f(ECS::EntityID, ECS::EntityManager&)> posFn)
		{
			std::vector<Node*> targets;
			forEachLeaf([&](Node& lf) {
				if (predicate(lf.chunk) && canSplit(lf)) targets.push_back(&lf);
				});
			for (Node* leaf : targets) subdivideAndReassign(*leaf, posFn);
		}

		void SubdivideIfOverCapacity(std::function<Math::Vec3f(ECS::EntityID, ECS::EntityManager&)> posFn)
		{
			SubdivideIf([&](const SpatialChunk& sc) {
				return sc.GetEntityManager().GetEntityCount() > m_maxPerLeafCount;
				}, std::move(posFn));
		}

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
			const auto [ix, iy] = leafIndex(*target);
			SpatialChunkKey newKey = MakeQuadKey(oldKey.level, target->depth, ix, iy, target->generation);

			target->chunk.SetNodeKey(newKey);

			reg.RegisterOwner(newKey, &target->chunk);
		}

		std::vector<SpatialChunk*> GetChunksAABB(const AABB& aabb)
		{
			std::vector<SpatialChunk*> out;
			if (!m_root) return out;
			queryAABB(*m_root, aabb, out); // 非 const 版
			return out;
		}
		std::vector<const SpatialChunk*> GetChunksAABB(const AABB& aabb) const
		{
			std::vector<const SpatialChunk*> out;
			if (!m_root) return out;
			queryAABB(*m_root, aabb, out); // const 版
			return out;
		}

		std::vector<SpatialChunk*> GetChunksCircle(const Circle& c)
		{
			std::vector<SpatialChunk*> out;
			if (!m_root) return out;
			queryCircle(*m_root, c, out); // 非 const 版
			return out;
		}
		std::vector<const SpatialChunk*> GetChunksCircle(const Circle& c) const
		{
			std::vector<const SpatialChunk*> out;
			if (!m_root) return out;
			queryCircle(*m_root, c, out); // const 版
			return out;
		}

		template<class PosFn>
		SpatialChunk* GetChunkForInsert(Math::Vec3f p, PosFn&& posFn)
		{
			SpatialChunk* sc = EnsureLeafForPoint(p);
			Node* leaf = findLeafByChunk(&m_root, sc);
			if (!leaf) return sc;

			auto& em = leaf->chunk.GetEntityManager();
			const size_t cnt = em.GetEntityCount();

			if (cnt > m_maxPerLeafCount && canSplit(*leaf)) {
				// 分割して再割り当て（全IDを子へ）
				subdivideAndReassign(*leaf, std::forward<PosFn>(posFn));

				// この点 p の子葉を返す
				const int qi = quadrant(*leaf, p.x, p.z);
				return &leaf->child[qi]->chunk;
			}
			return sc;
		}

		// 列挙ユーティリティ
		template<class F> void ForEachLeaf(F&& f) { forEachLeaf(std::forward<F>(f)); }
		template<class F> void ForEachLeaf(F&& f) const { forEachLeaf(std::forward<F>(f)); }

		template<class F> void ForEachLeafChunk(F&& f) {
			forEachLeaf([&](Node& n) { f(n.chunk); });
		}
		template<class F> void ForEachLeafChunk(F&& f) const {
			forEachLeaf([&](const Node& n) { f(n.chunk); });
		}

		template<class F> void ForEachLeafEM(F&& f) {
			forEachLeaf([&](Node& n) { f(n.chunk.GetEntityManager()); });
		}
		template<class F> void ForEachLeafEM(F&& f) const {
			forEachLeaf([&](const Node& n) { f(n.chunk.GetEntityManager()); });
		}

		// デバッグ用
		uint32_t LeafCount() const noexcept { return m_leafCount; }
		float MinLeafSize() const noexcept { return m_minLeaf; }

		void SetMaxPerLeafCount(uint32_t v) noexcept { m_maxPerLeafCount = v; }
		uint32_t GetMaxPerLeafCount() const noexcept { return m_maxPerLeafCount; }

		void SetMinPerLeafCount(uint32_t v) noexcept { m_minPerLeafCount = v; }
		uint32_t GetMinPerLeafCount() const noexcept { return m_minPerLeafCount; }

	private:
		struct Node {
			AABB bounds{};
			uint16_t generation = 0;
			uint8_t  depth = 0;
			std::array<std::unique_ptr<Node>, 4> child{}; // NW, NE, SW, SE
			SpatialChunk chunk; // 葉のみ実質使用

			bool isLeaf() const noexcept {
				return !child[0] && !child[1] && !child[2] && !child[3];
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

			const auto [ix, iy] = leafIndex(leafNode);
			SpatialChunkKey key = MakeQuadKey(level, leafNode.depth, ix, iy, /*gen*/leafNode.generation);
			sc.SetNodeKey(key);
			reg.RegisterOwner(key, &sc);
		}

		static bool intersects(const AABB& a, const AABB& b) noexcept {
			return !(a.ub.x <= b.lb.x || a.lb.x >= b.ub.x || a.ub.y <= b.lb.y || a.lb.y >= b.ub.y);
		}
		static bool intersects(const AABB& b, const Circle& c) noexcept {
			const float cx = std::clamp(c.cx, b.lb.x, b.ub.x);
			const float cy = std::clamp(c.cy, b.lb.y, b.ub.y);
			const float dx = cx - c.cx, dy = cy - c.cy;
			return (dx * dx + dy * dy) <= (c.r * c.r);
		}
		bool inBounds(float x, float z) const noexcept {
			return (0.f <= x && x < float(m_worldW)) && (0.f <= z && z < float(m_worldH));
		}
		bool canSplit(const Node& n) const noexcept {
			const float w = n.bounds.ub.x - n.bounds.lb.x;
			const float h = n.bounds.ub.y - n.bounds.lb.y;
			return (w > m_minLeaf) && (h > m_minLeaf);
		}
		int quadrant(const Node& n, float x, float z) const noexcept {
			const float mx = 0.5f * (n.bounds.lb.x + n.bounds.ub.x);
			const float mz = 0.5f * (n.bounds.lb.y + n.bounds.ub.y);

			const bool east = (x >= mx);
			bool north = (z >= mz);               // デフォは North
			if constexpr (kZPositive == ZPositive::South) north = !north;
			// 0: NW, 1: NE, 2: SW, 3: SE（X?Zでの方位）
			return (north ? 0 : 2) + (east ? 1 : 0);
		}

		void ensureChildren(Node& n)
		{
			if (!n.isLeaf()) return;
			const float mx = 0.5f * (n.bounds.lb.x + n.bounds.ub.x);
			const float mz = 0.5f * (n.bounds.lb.y + n.bounds.ub.y);
			const AABB quads[4] = {
				{ { n.bounds.lb.x, mz }, { mx,            n.bounds.ub.y } }, // NW
				{ { mx,            mz }, { n.bounds.ub.x, n.bounds.ub.y } }, // NE
				{ { n.bounds.lb.x, n.bounds.lb.y }, { mx, mz } },            // SW
				{ { mx,            n.bounds.lb.y }, { n.bounds.ub.x, mz } }  // SE
			};
			for (int i = 0; i < 4; ++i) {
				n.child[i] = std::make_unique<Node>();
				n.child[i]->bounds = quads[i];
				n.child[i]->depth = uint8_t(n.depth + 1);
				n.child[i]->generation = 0;
			}
			m_leafCount += 3; // 旧葉1 -> 子4 で +3
		}

		Node* descendToLeaf(Node& cur, float x, float z, bool createIfMissing) {
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
						// 子化に伴う“再配置 Split”を先に実行（ensureChildrenは内部でやる派）
						subdivideAndReassign(*n, posFn);  // Entity を子葉へ全て移す
					}
					else {
						ensureChildren(*n);               // 中身が空なら生成だけ
					}
				}
				const int qi = quadrant(*n, x, z);
				n = n->child[qi].get();
			}
			return n;
		}

		void queryAABB(Node& n, const AABB& q, std::vector<SpatialChunk*>& out)
		{
			if (!intersects(n.bounds, q)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) queryAABB(*n.child[i], q, out);
		}
		void queryAABB(const Node& n, const AABB& q, std::vector<const SpatialChunk*>& out) const
		{
			if (!intersects(n.bounds, q)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) queryAABB(*n.child[i], q, out);
		}

		void queryCircle(Node& n, const Circle& c, std::vector<SpatialChunk*>& out)
		{
			if (!intersects(n.bounds, c)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) queryCircle(*n.child[i], c, out);
		}
		void queryCircle(const Node& n, const Circle& c, std::vector<const SpatialChunk*>& out) const
		{
			if (!intersects(n.bounds, c)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) queryCircle(*n.child[i], c, out);
		}

		std::pair<uint32_t, uint32_t> leafIndex(const Node& n) const noexcept
		{
			const float base = (std::max)(1.0f, m_minLeaf);
			const float scale = 1.0f / base;
			const float lx = n.bounds.lb.x;
			const float ly = n.bounds.lb.y;
			const uint32_t ix = static_cast<uint32_t>(std::floor(lx * scale));
			const uint32_t iy = static_cast<uint32_t>(std::floor(ly * scale));
			return { ix, iy };
		}

		inline SpatialChunkKey MakeQuadKey(LevelID level, uint8_t depth,
			uint32_t ix, uint32_t iz, uint16_t gen = 0)
		{
			SpatialChunkKey k{};
			k.level = level;
			k.scheme = PartitionScheme::Quadtree2D;
			k.depth = depth;
			k.generation = gen;
			const uint64_t morton = Morton2D64(uint64_t(ix), uint64_t(iz)); // X,Z
			k.code = (uint64_t(depth) << 56) | (morton & 0x00FF'FFFF'FFFF'FFFFull);
			return k;
		}

		Node* findLeafByChunk(std::unique_ptr<Node>* pp, SpatialChunk* sc) const
		{
			if (!pp || !pp->get()) return nullptr;
			Node* n = pp->get();
			if (n->isLeaf()) return (&n->chunk == sc) ? n : nullptr;
			for (int i = 0; i < 4; ++i) {
				if (Node* r = findLeafByChunk(&n->child[i], sc)) return r;
			}
			return nullptr;
		}

		bool nodeIntersectsFrustum(const Node& n, const Math::Frustumf& fr,
			float ymin, float ymax) const noexcept
		{
			const float cx = 0.5f * (n.bounds.lb.x + n.bounds.ub.x);
			const float cz = 0.5f * (n.bounds.lb.y + n.bounds.ub.y);
			const float ex = 0.5f * (n.bounds.ub.x - n.bounds.lb.x);
			const float ez = 0.5f * (n.bounds.ub.y - n.bounds.lb.y);

			float cyEff, eyEff;
			if (!Math::Frustumf::ComputeYOverlapAtXZ(fr, cx, cz, ymin, ymax, cyEff, eyEff)) {
				return false; // 縦に重ならない
			}
			const Math::Vec3f center{ cx,  cyEff, cz };
			const Math::Vec3f extent{ ex,  eyEff, ez };
			return fr.IntersectsAABB(center, extent);
		}

		void cullRecursive(Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<SpatialChunk*>& out)
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}

		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<const SpatialChunk*>& out) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(&n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}

		template<class F>
		void cullRecursive(Node& n, const Math::Frustumf& fr, float ymin, float ymax, F&& f)
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) f(n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, f);
		}

		template<class F>
		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax, F&& f) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) f(n.chunk); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, f);
		}

		void cullRecursive(const Node& n, const Math::Frustumf& fr, float ymin, float ymax,
			std::vector<AABB>& out) const
		{
			if (!nodeIntersectsFrustum(n, fr, ymin, ymax)) return;
			if (n.isLeaf()) { if (n.chunk.GetEntityManager().GetEntityCount() > 0) out.push_back(n.bounds); return; }
			for (int i = 0; i < 4; ++i) if (n.child[i]) cullRecursive(*n.child[i], fr, ymin, ymax, out);
		}

		template<class F>
		void forEachLeaf(F&& f)
		{
			if (!m_root) return;
			std::vector<Node*> stack; stack.reserve(64);
			stack.push_back(m_root.get());
			while (!stack.empty()) {
				Node* n = stack.back(); stack.pop_back();
				if (n->isLeaf()) { f(*n); continue; }
				for (int i = 0; i < 4; ++i) if (n->child[i]) stack.push_back(n->child[i].get());
			}
		}
		template<class F>
		void forEachLeaf(F&& f) const
		{
			if (!m_root) return;
			std::vector<const Node*> stack; stack.reserve(64);
			stack.push_back(m_root.get());
			while (!stack.empty()) {
				const Node* n = stack.back(); stack.pop_back();
				if (n->isLeaf()) { f(*n); continue; }
				for (int i = 0; i < 4; ++i) if (n->child[i]) stack.push_back(n->child[i].get());
			}
		}

		void subdivideAndReassign(Node& leaf,
			const std::function<std::optional<Math::Vec3f>(ECS::EntityID, ECS::EntityManager&)>& posFn)
		{
			if (!leaf.isLeaf() || !canSplit(leaf)) return;

			// 1) 子生成（葉→内部）
			ensureChildren(leaf);

			ECS::EntityManager& src = leaf.chunk.GetEntityManager();

			// 2) ルータ: id -> 位置 -> 子の EM を返す
			auto router = [&](ECS::EntityID id, const ECS::ComponentMask /*mask*/) -> ECS::EntityManager* {
				const std::optional<Math::Vec3f> pos = posFn(id, src);          // 位置は "今ここ" の EM から見える
				if (!pos) return nullptr;
				const int qi = quadrant(leaf, pos->x, pos->z);      // xz で子決定
				return &leaf.child[qi]->chunk.GetEntityManager();  // 宛先 EM
				};

			// 3) 一括分割移送（非スパース→IDごと、スパース→バケット一括）
			//    SplitByAll は dst.EM へ move + src 側からローカル除去まで面倒見ます
			(void)src.SplitByAll(router); // 戻り値=移送数（必要なら使用）
			//   - SplitByAllの仕様: ルータが返す EM ごとに非スパースを先行移送し、最後にスパースをまとめて移送（IDバケット）
			//     -> EntityManager.h / .cpp 参照
			//     （この呼び出しで親葉のデータは "空" になる想定）
			//     参考: SplitByAll / MoveSparseIDsTo の存在と役割。:contentReference[oaicite:3]{index=3} :contentReference[oaicite:4]{index=4}

			// 4) 親ノード世代を更新（RegisterAllChunks 時にキーへ反映）
			++leaf.generation;
		}

		/**
		 * @brief 統合（子4葉->親葉）
		 * @return マージした親ノードの深度
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

			// 子→親の順で評価（後置）
			for (auto it = post.rbegin(); it != post.rend(); ++it) {
				Node* n = *it;
				// 子が4つ揃っていて全て葉？
				bool allLeaf = true;
				for (int i = 0; i < 4; ++i) { if (!n->child[i] || !n->child[i]->isLeaf()) { allLeaf = false; break; } }
				if (!allLeaf) continue;

				// 合計件数
				size_t sum = 0;
				for (int i = 0; i < 4; ++i) sum += n->child[i]->chunk.GetEntityManager().GetEntityCount();

				if (sum <= m_minPerLeafCount) {
					// 1) 子→親へ統合
					auto& dst = n->chunk.GetEntityManager();
					for (int i = 0; i < 4; ++i) {
						auto& src = n->child[i]->chunk.GetEntityManager();
						(void)dst.MergeFromAll(src); // 全移送（非スパース＋スパース）:contentReference[oaicite:5]{index=5} :contentReference[oaicite:6]{index=6}
					}
					// 2) 子を破棄して葉に戻す
					n->child = {};
					++n->generation;
					m_leafCount -= 3; // 4->1
					++mergedParents;
				}
			}
			return mergedParents;
		}

	private:
		ECS::EntityManager m_global;
		std::unique_ptr<Node> m_root;

		ChunkSizeType m_worldW = 0;
		ChunkSizeType m_worldH = 0;
		float m_minLeaf = 1.0f;

		uint32_t m_minPerLeafCount = 0; // 統合のトリガ（子4合計がこれ未満なら親に統合）
		uint32_t m_maxPerLeafCount = 1024;

		uint32_t m_leafCount = 1;

		double m_coalesceTimer = 0.0;
	};

	// ==== Query 特殊化 ====
	namespace ECS {
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(SFW::QuadTreePartition& ctx) const noexcept
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

			collect_from(ctx.GetGlobalEntityManager());

			ctx.ForEachLeafEM([&](const ECS::EntityManager& em) {
				collect_from(em);
				});

			return result;
		}
	} // namespace ECS
} // namespace SectorFW
