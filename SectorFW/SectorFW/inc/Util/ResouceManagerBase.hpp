#pragma once

#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "Util/CopyableAtomic.hpp"

namespace SectorFW
{
	template<typename Derived, typename HandleType, typename CreateDescType, typename ResourceType>
	class ResourceManagerBase {
	public:
		struct Slot {
			ResourceType data;
			uint32_t generation = 0;
			bool alive = false;
		};

		// Add: 既存があれば再利用(+1)＆削除要求が入っていればキャンセル
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
			uint32_t idx;
			{
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
			}
			HandleType h{ idx, slots[idx].generation };

			slots[idx].data = static_cast<Derived*>(this)->CreateResource(desc, h);
			slots[idx].alive = true;
			refCount[idx].store(1, std::memory_order_relaxed);

			static_cast<Derived*>(this)->RegisterKey(desc, h);

			out = h;
			return false;
		}

		void AddRef(HandleType h) {
			assert(IsValid(h));
			refCount[h.index].fetch_add(1, std::memory_order_relaxed);
		}

		// Release: 0 になったら削除要求を積む（alive はここでは落とさない）
		void Release(HandleType h, uint64_t deleteSync = 0) {
			assert(IsValid(h));
			auto prev = refCount[h.index].fetch_sub(1, std::memory_order_acq_rel);
			assert(prev > 0 && "Release underflow");
			if (prev == 1) EnqueueDelete(h.index, deleteSync);
		}

		// 削除要求の登録（重複を防いで期限更新）
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

		// 削除要求のキャンセル（Add での復活時など）
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

		const ResourceType& Get(HandleType h) const {
			std::shared_lock<std::shared_mutex> lock(mapMutex);

			assert(IsValid(h));
			return slots[h.index].data;
		}

		// 期限到達で最終判断：ref==0 なら破棄、>0 なら削除キャンセル
		void ProcessDeferredDeletes(uint64_t currentFrame) {
			std::lock_guard lk(pendingMutex_);
			size_t i = 0;
			while (i < pendingDelete.size()) {
				auto& req = pendingDelete[i];
				if (currentFrame < req.deleteSync) { ++i; continue; }

				const uint32_t idx = req.index;
				if (refCount[idx].load(std::memory_order_acquire) == 0) {
					// ★ ここで初めて alive=false。以降は Derived の責務。
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

		bool IsValid(HandleType h) const {
			return h.index < slots.size() &&
				slots[h.index].generation == h.generation &&
				slots[h.index].alive;
		}
	protected:
		// 派生が使うユーティリティ
		void MarkDead(uint32_t index) {
			std::unique_lock lock(mapMutex);
			slots[index].alive = false;
		}
		void FreeIndex(uint32_t index) {
			std::unique_lock lock(mapMutex);
			freeList.push_back(index);
		}

		std::vector<Slot> slots;
		std::vector<CopyableAtomic<uint32_t>> refCount;
		std::vector<uint32_t> freeList;

	private:
		//Get関数のconstを無効にするのmutable
		mutable std::shared_mutex mapMutex;
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