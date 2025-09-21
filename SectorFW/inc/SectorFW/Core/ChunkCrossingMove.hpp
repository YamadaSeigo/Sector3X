/*****************************************************************//**
 * @file   ChunkCrossingMove.hpp
 * @brief チャンク跨ぎ検出とエンティティ移送のユーティリティ
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <optional>
#include <vector>
#include <deque>
#include <cassert>

#include "ECS/EntityManager.h"
#include "partition.hpp"   // SpatialChunk / SpatialChunkKey / EOutOfBoundsPolicy / Registry

namespace SectorFW
{
	/**
	 * @brief 基本ハンドル（キー主・ポインタ従の二段構え）
	 */
	struct ChunkHandle {
		SpatialChunkKey key{};        // 常に真のソース（Quad/Octは必須）
		SpatialChunk* cached = nullptr; // 任意の高速キャッシュ（無効化OK）
		bool is_valid() const { return key.code != 0 || cached != nullptr; }
	};

	/**
	 * @brief PartitionTraits: ポインタ安定性で切替（デフォルトfalse）
	 */
	template<class Partition> struct PartitionTraits { static constexpr bool stable_ptr = false; };

	class Grid2DPartition; class Grid3DPartition; class QuadTreePartition; class OctreePartition;
	template<> struct PartitionTraits<Grid2DPartition> { static constexpr bool stable_ptr = true; };
	template<> struct PartitionTraits<Grid3DPartition> { static constexpr bool stable_ptr = true; };
	template<> struct PartitionTraits<QuadTreePartition> { static constexpr bool stable_ptr = false; };
	template<> struct PartitionTraits<OctreePartition> { static constexpr bool stable_ptr = false; };

	/**
	 * @brief 低レベル：EM間 移送（非スパース→スパース）
	 * @param id 移送するエンティティID
	 * @param src 移送元のEM
	 * @param dst　移送先のEM
	 * @return　成功したらtrue、失敗したらfalse
	 */
	inline bool RelocateEntityBetweenManagers(ECS::EntityID id, ECS::EntityManager& src, ECS::EntityManager& dst)
	{
		if (&src == &dst) return false;
		if (!src.InsertWithID_ForManagerMove(id, src, dst)) return false;
		std::vector<ECS::EntityID> one{ id };
		src.MoveSparseIDsTo(dst, one);
		return true;
	}
	/**
	 * @brief 低レベル：チャンクキーからチャンク取得
	 * @param h ハンドル
	 * @param reg チャンクレジストリ
	 * @return 解決したチャンクポインタ（失敗時nullptr）
	 */
	inline SpatialChunk* ResolveChunk(ChunkHandle& h, SpatialChunkRegistry& reg)
	{
		if (h.key.code == 0) { h.cached = nullptr; return nullptr; }
		SpatialChunk* sc = reg.ResolveOwner(h.key);
		h.cached = sc;
		return sc;
	}

	/**
	 * @brief 単体：チャンク跨ぎ検出 → 必要時のみEM間移送
	 * @tparam Partition 分割クラス
	 * @param id 移送するエンティティID
	 * @param newPos 新しい位置
	 * @param partition 分割クラス
	 * @param reg チャンクレジストリ
	 * @param level レベルID
	 * @param handle チャンクハンドル
	 * @param policy 範囲外ポリシー（デフォルトClampToEdge）
	 */
	template<class Partition>
	inline bool MoveIfCrossed(ECS::EntityID id,
		const Math::Vec3f& newPos,
		Partition& partition,
		SpatialChunkRegistry& reg,
		LevelID level,
		ChunkHandle& handle,
		EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge)
	{
		auto dstOpt = partition.GetChunk(newPos, reg, level, policy);
		if (!dstOpt) return false;
		SpatialChunk* dst = *dstOpt;

		if constexpr (PartitionTraits<Partition>::stable_ptr) {
			if (dst == handle.cached) return false;
			if (handle.cached) {
				auto& srcEM = handle.cached->GetEntityManager();
				auto& dstEM = dst->GetEntityManager();
				if (&srcEM != &dstEM) RelocateEntityBetweenManagers(id, srcEM, dstEM);
			}
			handle.cached = dst;
			handle.key = dst->GetNodeKey();
			return true;
		}
		else {
			const SpatialChunkKey dstKey = dst->GetNodeKey();
			if (dstKey.code == handle.key.code && dstKey.level == handle.key.level) {
				handle.cached = dst;
				return false;
			}
			ECS::EntityManager* srcEM = reg.ResolveOwner(handle.key);
			ECS::EntityManager* dstEM = reg.ResolveOwner(dstKey);
			if (srcEM && dstEM && srcEM != dstEM) {
				RelocateEntityBetweenManagers(id, *srcEM, *dstEM);
			}
			handle.key = dstKey;
			handle.cached = dst;
			return true;
		}
	}

	/**
	 * @brief バッチ：葉 / セル単位で一気に仕分け
	 * @tparam Partition 分割クラス
	 * @param partition 分割クラス
	 * @param srcChunk 移送元チャンク
	 * @param reg チャンクレジストリ
	 * @param level レベルID
	 * @param positionFn 位置取得関数（引数：EntityID, EntityManager&、戻り値：Vec3f）
	 */
	template<class Partition, class PositionFn>
	inline size_t RelocateCrossedBatch(Partition& partition,
		SpatialChunk& srcChunk,
		SpatialChunkRegistry& reg,
		LevelID level,
		PositionFn positionFn)
	{
		ECS::EntityManager& src = srcChunk.GetEntityManager();
		auto router = [&](ECS::EntityID id, const ECS::ComponentMask) -> ECS::EntityManager* {
			Math::Vec3f p = positionFn(id, src);
			auto dstOpt = partition.GetChunk(p, reg, level, EOutOfBoundsPolicy::ClampToEdge);
			if (!dstOpt) return &src;
			return &(*dstOpt)->GetEntityManager();
			};
		return src.SplitByAll(router);
	}
	/**
	 * @brief 退避ステート（Float EM）
	 */
	enum class SpatialState : uint8_t { Attached, Detached };
	/**
	 * @brief 動的エンティティ用タグ（チャンク跨ぎ検出とEM間移送の状態管理）
	 */
	struct SpatialMotionTag {
		ChunkHandle handle;
		SpatialChunkKey pendingKey{};
		uint16_t stableFrames = 0;
		SpatialState state = SpatialState::Attached;
	};

	struct SettleRule { float vThreshold = 0.2f; int frames = 5; };

	template<class Partition>
	void UpdateSpatialAttachment(ECS::EntityID id,
		const Math::Vec3f& pos,
		const Math::Vec3f& vel,
		Partition& partition,
		SpatialChunkRegistry& reg,
		LevelID level,
		SpatialMotionTag& tag,
		ECS::EntityManager& floatEM,
		const SettleRule& rule)
	{
		auto dstOpt = partition.GetChunk(pos, reg, level, EOutOfBoundsPolicy::ClampToEdge);
		const SpatialChunkKey dstKey = (dstOpt ? (*dstOpt)->GetNodeKey() : SpatialChunkKey{});
		bool moving = (vel.length() > rule.vThreshold);

		if (tag.state == SpatialState::Attached) {
			if (moving) {
				auto* srcEM = reg.ResolveOwnerEM(tag.handle.key);
				if (srcEM && srcEM != &floatEM) RelocateEntityBetweenManagers(id, *srcEM, floatEM);
				tag.state = SpatialState::Detached;
				tag.pendingKey = dstKey;
				tag.stableFrames = 0;
			}
			else {
				MoveIfCrossed(id, pos, partition, reg, level, tag.handle);
			}
		}
		else {
			tag.pendingKey = dstKey;
			if (!moving) {
				if (++tag.stableFrames >= rule.frames && dstKey.code) {
					auto* dstEM = reg.ResolveOwnerEM(dstKey);
					if (dstEM && dstEM != &floatEM) {
						RelocateEntityBetweenManagers(id, floatEM, *dstEM);
						tag.handle.key = dstKey;
						tag.handle.cached = (dstOpt ? *dstOpt : nullptr);
						tag.state = SpatialState::Attached;
					}
				}
			}
			else {
				tag.stableFrames = 0;
			}
		}
	}
	/**
	 * @brief 予算付きバッチ移送クラス
	 */
	class BudgetMover {
	public:
		struct PendingMove { ECS::EntityID id; SpatialChunkKey srcKey{}; SpatialChunkKey dstKey{}; };

		struct Accessor {
		private:
			static inline void MoverFlush(BudgetMover& mover, SpatialChunkRegistry& reg, size_t budget) {
				mover.Flush(reg, budget);
			}

			template<PartitionConcept T>
			friend class Level;
		};

		/**
		 * @brief ローカル一時バッファ（thread_local を使わない合意的バッチ）
		 */
		class LocalBatch {
		public:
			explicit LocalBatch(BudgetMover& owner, size_t reserve_n = 0) noexcept
				: owner_(&owner) {
				if (reserve_n) buf_.reserve(reserve_n);
			}

			// Move-only（所有権の二重Flushを防ぐ）
			LocalBatch(LocalBatch&& o) noexcept : owner_(o.owner_), buf_(std::move(o.buf_)) { o.owner_ = nullptr; }
			LocalBatch& operator=(LocalBatch&& o) noexcept {
				if (this == &o) return *this;
				FlushNoThrow();
				owner_ = o.owner_; o.owner_ = nullptr;
				buf_ = std::move(o.buf_);
				return *this;
			}
			LocalBatch(const LocalBatch&) = delete;
			LocalBatch& operator=(const LocalBatch&) = delete;

			~LocalBatch() noexcept { FlushNoThrow(); }

			inline void Add(ECS::EntityID id, const SpatialChunkKey& src, const SpatialChunkKey& dst) {
				if (src == dst) return; // 必要なら IsValid(src/dst) へ変更
				buf_.push_back({ id, src, dst });
			}
			template<class Range>
			inline void AddRange(const Range& moves) { buf_.insert(buf_.end(), moves.begin(), moves.end()); }

			inline void Flush() { if (buf_.empty() || owner_ == nullptr) return; owner_->EnqueueBulk(buf_); buf_.clear(); }
			inline void FlushNoThrow() noexcept { if (buf_.empty() || owner_ == nullptr) return; try { owner_->EnqueueBulk(buf_); } catch (...) {/*最後の手段：捨てずに保持*/ } buf_.clear(); }

			inline void ClearKeepCapacity() { buf_.clear(); }
			inline void ClearAndRelease() { std::vector<PendingMove>().swap(buf_); }
			inline void Cancel() noexcept { owner_ = nullptr; buf_.clear(); }
			size_t Size() const { return buf_.size(); }
		private:
			BudgetMover* owner_{};                  // ※ owner_ の寿命 > このバッチの寿命であること
			std::vector<PendingMove> buf_{};        // 呼び出し側が寿命管理するローカル一時
		};

	private:
		mutable std::mutex mtx_;                // Enqueue/Flush で共有キューを保護
		std::vector<PendingMove> queue_;        // 共有キュー（複数スレッドから積まれる）
		std::vector<PendingMove> temp_;         // Flush 用一時
	public:
		// 即時ではなく「後で」移すために積む
		inline void Enqueue(ECS::EntityID id, const SpatialChunkKey& src, const SpatialChunkKey& dst) {
			// ここでキー妥当性チェックを入れる場合は IsValid(src/dst) に置き換えてください
			if (src == dst) return;
			std::lock_guard<std::mutex> lock(mtx_);
			queue_.push_back({ id, src, dst });
		}

		// まとめ積み（ロックを1回に）
		template<class Range>
		inline void EnqueueBulk(const Range& moves) {
			std::lock_guard<std::mutex> lock(mtx_);
			queue_.insert(queue_.end(), moves.begin(), moves.end());
		}

		// そのフレームの上限 budget 件まで処理し、残りは次フレームへ持ち越す
		size_t Flush(SpatialChunkRegistry& reg, size_t budget) {
			// 共有キューから先頭 budget 件を取り出す（臨界区間は最小限）
			if (budget == 0) return 0;

			temp_.clear();
			{
				std::lock_guard<std::mutex> lock(mtx_);
				if (queue_.empty()) return 0;
				const size_t n = (std::min)(queue_.size(), budget);
				temp_.assign(queue_.begin(), queue_.begin() + n);
				queue_.erase(queue_.begin(), queue_.begin() + n);
			}

			// ここからはロック無しで処理
			struct EMKeyPair { ECS::EntityManager* src; ECS::EntityManager* dst; };
			struct PairHash { size_t operator()(const EMKeyPair& p) const noexcept { return (reinterpret_cast<uintptr_t>(p.src) >> 3) ^ (reinterpret_cast<uintptr_t>(p.dst) << 1); } };
			struct PairEq { bool operator()(const EMKeyPair& a, const EMKeyPair& b) const noexcept { return a.src == b.src && a.dst == b.dst; } };

			std::unordered_map<EMKeyPair, std::vector<ECS::EntityID>, PairHash, PairEq> buckets;
			buckets.reserve(temp_.size());

			for (const PendingMove& pm : temp_) {
				ECS::EntityManager* srcEM = reg.ResolveOwnerEM(pm.srcKey);
				ECS::EntityManager* dstEM = reg.ResolveOwnerEM(pm.dstKey);
				if (!srcEM || !dstEM || srcEM == dstEM) continue;
				buckets[{srcEM, dstEM}].push_back(pm.id);
			}

			size_t moved = 0;
			for (auto& [pair, ids] : buckets) {
				if (ids.empty()) continue;
				ECS::EntityManager& src = *pair.src;
				ECS::EntityManager& dst = *pair.dst;

				for (ECS::EntityID id : ids) {
					bool ok = ECS::EntityManager::InsertWithID_ForManagerMove(id, src, dst);
					if (ok) ++moved;
				}
				src.MoveSparseIDsTo(dst, ids);
			}
			temp_.clear();
			return moved;
		}

		// キューを全破棄
		void Clear() { queue_.clear(); temp_.clear(); }
		size_t Size() const { return queue_.size(); }
	};
	/**
	 * @brief 即時→ディファード版 MoveIfCrossed（移送はキューへ）
	 * @detail * Flush をフレーム末に呼んで実際に動かす
	 * @detail スレッド終わりに必ず BudgetMover::PublishTLS() を呼ぶこと
	 */
	template<class Partition>
	inline bool MoveIfCrossed_Deferred(ECS::EntityID id,
		const Math::Vec3f& newPos,
		Partition& partition,
		SpatialChunkRegistry& reg,
		LevelID level,
		ChunkHandle& handle,
		BudgetMover::LocalBatch& moverBatch,
		EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge)
	{
		std::optional<SpatialChunk*> dstOpt = partition.GetChunk(newPos, reg, level, policy);
		if (!dstOpt) return false;
		SpatialChunk* dst = *dstOpt;

		if constexpr (PartitionTraits<Partition>::stable_ptr) {
			if (dst == handle.cached) return false;
			// 旧所属があれば、(srcKey,dstKey) を積む
			if (handle.cached) {
				moverBatch.Add(id, handle.key, dst->GetNodeKey());
			}
			handle.cached = dst;
			handle.key = dst->GetNodeKey();
			return true;
		}
		else {
			const SpatialChunkKey dstKey = dst->GetNodeKey();
			if (dstKey.code == handle.key.code && dstKey.level == handle.key.level) {
				handle.cached = dst;
				return false;
			}
			// 実移送せず、(srcKey,dstKey) を積む
			if (handle.key.code) moverBatch.Add(id, handle.key, dstKey);
			handle.key = dstKey;
			handle.cached = dst;
			return true;
		}
	}
} // namespace SectorFW