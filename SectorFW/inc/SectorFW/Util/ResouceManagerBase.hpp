/*****************************************************************//**
 * @file   ResouceManagerBase.hpp
 * @brief リソースマネージャーの基底クラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "Util/CopyableAtomic.hpp"

namespace SectorFW
{
	/**
	 * @brief リソースマネージャーの基底クラス。主にRenderer系で使用する。
	 */
	template<typename Derived, typename HandleType, typename CreateDescType, typename ResourceType>
	class ResourceManagerBase {
	public:
		/**
		 * @brief リソースをShared Lock付きで取得するためのラッパー
		 */
		struct Resource {
			Resource(const ResourceType& resource, std::shared_mutex& mutex) : data(resource), lock(mutex) {}

			Resource(Resource&& other) : data(other.data), lock(std::move(other.lock)) {}

			// コピー禁止
			Resource(const Resource&) = delete;
			Resource& operator=(const Resource&) = delete;

			Resource& operator=(Resource&& other) noexcept {
				if (this != &other) {
					data = other.data;
					lock = std::move(other.lock);
				}
				return *this;
			}

			inline const ResourceType& operator*() const& noexcept { return data; } // lvalueを許可
			const ResourceType& operator*() const&& = delete;		// rvalueは不可

			inline const ResourceType& ref() const& noexcept { return data; } // lvalueを許可
			const ResourceType& ref() const&& = delete;  	// rvalueは不可
		private:
			const ResourceType& data;
			std::shared_lock<std::shared_mutex> lock;
		};
		/**
		 * @brief リソースが有効かどうかをチェックする関数
		 */
		struct Slot {
			ResourceType data;
			uint32_t generation = 0;
			bool alive = false;
		};

		/**
		 * @brief Add: 既存があれば再利用(+1)＆削除要求が入っていればキャンセル
		 * @param desc 作成情報
		 * @param out 取得したハンドル
		 * @return 既存を再利用した場合 true、新規作成なら false
		 */
		bool Add(const CreateDescType& desc, HandleType& out) {
			if (auto h = static_cast<Derived*>(this)->FindExisting(desc)) {
				// 既存を +1
				AddRef(*h);
				// もし削除要求が入っていればキャンセル
				CancelPending((*h).index);

				out = *h;
				return true;
			}

			// 新規確保
			HandleType h = AllocateHandle();

			slots[h.index].data = static_cast<Derived*>(this)->CreateResource(desc, h);
			slots[h.index].alive = true;
			refCount[h.index].store(1, std::memory_order_relaxed);

			static_cast<Derived*>(this)->RegisterKey(desc, h);

			out = h;
			return false;
		}
		/**
		 * @brief AddRef: 参照カウント +1
		 * @param h 有効なハンドル
		 */
		void AddRef(HandleType h) {
			assert(IsValid(h));
			refCount[h.index].fetch_add(1, std::memory_order_relaxed);
		}
		/**
		 * @brief Release: 0 になったら削除要求を積む（alive はここでは落とさない）
		 * @param h 有効なハンドル
		 * @param deleteSync 削除要求の期限（フレームカウンタなど）。0 なら即時。
		 */
		void Release(HandleType h, uint64_t deleteSync = 0) {
			assert(IsValid(h));
			auto prev = refCount[h.index].fetch_sub(1, std::memory_order_acq_rel);
			assert(prev >= 0 && "Release underflow");
			if (prev == 0) EnqueueDelete(h.index, deleteSync);
		}
		/**
		 * @brief 削除要求の登録（重複を防いで期限更新）
		 * @param index スロットインデックス
		 * @param deleteSync 削除要求の期限（フレームカウンタなど）。0 なら即時。
		 */
		void EnqueueDelete(uint32_t index, uint64_t deleteSync) {
			std::lock_guard lk(pendingMutex_);
			if (auto it = pendingByIndex.find(index); it == pendingByIndex.end()) {
				pendingDelete.push_back({ index, deleteSync });
				pendingByIndex[index] = pendingDelete.size() - 1;
			}
			else {
				pendingDelete[it->second].deleteSync = deleteSync; // 後ろへ延ばす
			}
		}
		/**
		 * @brief 削除要求のキャンセル（Add での復活時など）
		 * @param index スロットインデックス
		 */
		void CancelPending(uint32_t index) {
			std::lock_guard lk(pendingMutex_);
			auto it = pendingByIndex.find(index);
			if (it != pendingByIndex.end()) {
				const size_t pos = it->second;
				pendingDelete.erase(pendingDelete.begin() + pos);
				pendingByIndex.erase(it);
				// pos 以降の位置がずれるので、必要なら詰め替え最適化を入れてもよい
			}
		}
		/**
		 * @brief Get: 有効なハンドルなら Shared Lock 付きでリソースを返す
		 * @param h 有効なハンドル
		 * @return Resource リソースのラッパー
		 */
		[[nodiscard]] Resource Get(HandleType h) const {
			assert(IsValid(h));
			return { slots[h.index].data, mapMutex };
		}
		/**
		 * @brief GetDirect: インデックス直指定で Shared Lock 付きでリソースを返す（IsValid チェックなし）
		 * @param idx スロットインデックス
		 * @return Resource リソースのラッパー
		 */
		[[nodiscard]] Resource GetDirect(uint32_t idx) const {
			return { slots[idx].data, mapMutex };
		}
		/**
		 * @brief 期限到達で最終判断：ref == 0 なら破棄、>0 なら削除キャンセル
		 * @param currentFrame 現在のフレームカウンタなど
		 */
		void ProcessDeferredDeletes(uint64_t currentFrame) {
			std::lock_guard lk(pendingMutex_);
			size_t i = 0;
			while (i < pendingDelete.size()) {
				auto& req = pendingDelete[i];
				if (currentFrame < req.deleteSync) { ++i; continue; }

				const uint32_t idx = req.index;
				if (refCount[idx].load(std::memory_order_acquire) == 0) {
					// ここで初めて alive=false。以降は Derived の責務。
					slots[idx].alive = false;

					// 登録済みキーや名前→handleなどを掃除
					static_cast<Derived*>(this)->RemoveFromCaches(idx);

					// 実体破棄（子の Release 連鎖などがあれば currentFrame を渡して遅延にも対応）
					static_cast<Derived*>(this)->DestroyResource(idx, currentFrame);

					// スロット再利用
					freeList.push_back(idx);
				}
				// 破棄済み or キャンセル、どちらでも要求は捨てる
				pendingByIndex.erase(idx);
				pendingDelete.erase(pendingDelete.begin() + i);
			}
		}
		/**
		 * @brief ハンドルが有効かどうかをチェックする関数
		 * @param h チェックするハンドル
		 * @return bool 有効な場合はtrue、そうでない場合はfalse
		 */
		bool IsValid(HandleType h) const {
			return h.index < slots.size() &&
				slots[h.index].generation == h.generation &&
				slots[h.index].alive;
		}
	protected:
		/**
		 * @brief ハンドルを新規確保する関数
		 * @return HandleType 新規確保したハンドル
		 */
		HandleType AllocateHandle() {
			uint32_t idx;
			std::unique_lock lock(mapMutex);
			if (!freeList.empty()) {
				idx = freeList.back();
				freeList.pop_back();
				slots[idx].generation++;
			}
			else {
				idx = static_cast<uint32_t>(slots.size());
				slots.push_back({});
				refCount.push_back(0);
			}
			return { idx, slots[idx].generation };
		}
		/**
		 * @brief 派生が使うユーティリティ
		 * @detail 指定したインデックスのスロットを死んでいる状態にする（ProcessDeferredDeletes での最終破棄までの間に使う）
		 * @param index スロットインデックス
		 */
		void MarkDead(uint32_t index) {
			std::unique_lock lock(mapMutex);
			slots[index].alive = false;
		}
		/**
		 * @brief 派生が使うユーティリティ
		 * @detail 指定したインデックスのスロットを即時に解放する（ProcessDeferredDeletes での最終破棄までの間に使う）
		 * @param index スロットインデックス
		 */
		void FreeIndex(uint32_t index) {
			std::unique_lock lock(mapMutex);
			freeList.push_back(index);
		}
		/**
		 * @brief スロット情報のリスト
		 */
		std::vector<Slot> slots;
		/**
		 * @brief 参照カウントのリスト
		 */
		std::vector<CopyableAtomic<uint32_t>> refCount;
		/**
		 * @brief 空きスロットインデックスのリスト
		 */
		std::vector<uint32_t> freeList;

	protected:
		//Get関数のconstを無効にするのmutable
		mutable std::shared_mutex mapMutex;
	private:
		// 削除要求キュー
		struct PendingDelete { uint32_t index; uint64_t deleteSync; };

		std::vector<PendingDelete> pendingDelete;
		std::unordered_map<uint32_t, size_t> pendingByIndex; // index -> pendingDelete_ の位置
		std::mutex pendingMutex_;
	};

	//速度重視でハッシュで比較する。安全にするならmemcmpを使う。
	inline size_t HashBufferContent(const void* data, size_t size) {
		std::hash<std::string_view> hasher;
		return hasher(std::string_view((const char*)data, size));
	}
}