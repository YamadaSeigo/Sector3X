#pragma once

#include "RenderTypes.h"

#include <vector>
#include <mutex>
#include <shared_mutex>

#include "Util/CopyableAtomic.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		template<typename Derived, typename HandleType, typename CreateDescType, typename ResourceType>
		class ResourceManagerBase {
		public:
			struct Slot {
				ResourceType data;
				uint32_t generation = 0;
				bool alive = false;
			};

			HandleType Add(const CreateDescType& desc) {
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

				slots[idx].data = static_cast<Derived*>(this)->CreateResource(desc);
				slots[idx].alive = true;

				return { idx, slots[idx].generation };
			}

			void AddRef(HandleType h) {
				refCount[h.index].fetch_add(1, std::memory_order_relaxed);
			}

			void Release(HandleType h, uint64_t deleteSyncValue) {
				if (refCount[h.index].fetch_sub(1, std::memory_order_acq_rel) == 1) {
					std::lock_guard lock(deleteMutex);
					static_cast<Derived*>(this)->ScheduleDestroy(h.index, deleteSyncValue);
				}
			}

			const ResourceType& Get(HandleType h) const {
				std::shared_lock lock(mapMutex);

				assert(IsValid(h));
				return slots[h.index].data;
			}
			/**
			 * @brief フレームカウントを数えて、削除予定のリソースを処理する
			 * @detail マルチスレッド非対応
			 * @param syncValue 同期値、削除予定のリソースがこの値以下のフレームで削除される
			 */
			void ProcessDeferredDeletes(uint64_t syncValue) {
				static_cast<Derived*>(this)->ProcessDeferredDeletes(syncValue);
			}

		protected:
			bool IsValid(HandleType h) const {
				return h.index < slots.size() &&
					slots[h.index].generation == h.generation &&
					slots[h.index].alive;
			}

			std::vector<Slot> slots;
			std::vector<CopyableAtomic<uint32_t>> refCount;
			std::vector<uint32_t> freeList;

			//Get関数のconstを無効にするのmutable
			mutable std::shared_mutex mapMutex;
			std::mutex deleteMutex;
		};

		inline size_t HashBufferContent(const void* data, size_t size) {
			std::hash<std::string_view> hasher;
			return hasher(std::string_view((const char*)data, size));
		}
	}
}