#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <cassert>
#include "../external/concurrentqueue/concurrentqueue.h"
#include "entity.h"

namespace SectorFW
{
    namespace ECS
    {
        // Thread-safe entity ID manager
        class EntityIDAllocator {
        public:
            explicit EntityIDAllocator(size_t maxEntities)
                : maxEntities(static_cast<uint32_t>(maxEntities)),
                generations(maxEntities),
                nextIndex(0),
                freeQueue(static_cast<int>(maxEntities))
            {
                // Initially empty
            }

            EntityID Create() {
                uint32_t index;

				// 再利用できるインデックスを取得
                if (freeQueue.try_dequeue(index)) {
                    uint32_t gen = generations[index].load(std::memory_order_acquire);
                    return EntityID{ index, gen };
                }

				// 新しいインデックスを割り当て
                index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (index >= maxEntities) {
                    return EntityID::Invalid(); // Exhausted
                }

				// 新しいIDの世代を0に初期化
                generations[index].store(0, std::memory_order_release);
                return EntityID{ index, 0 };
            }

            void Destroy(EntityID id) {
                if (id.index >= maxEntities) return;

                // Invalidate old ID
                generations[id.index].fetch_add(1, std::memory_order_acq_rel);

                // Reuse the index
                bool success = freeQueue.try_enqueue(id.index);
                if (!success) {
                    // Free queue full → ID leak（無視してもよい or ログ出力）
                }
            }

            bool IsAlive(EntityID id) const {
                if (id.index >= maxEntities) return false;
                return generations[id.index].load(std::memory_order_acquire) == id.generation;
            }

            uint32_t Capacity() const { return maxEntities; }
			uint32_t NextIndex() const { return nextIndex.load(std::memory_order_acquire); }

        private:
            const uint32_t maxEntities;

            std::atomic<uint32_t> nextIndex;
            std::vector<std::atomic<uint32_t>> generations;

            moodycamel::ConcurrentQueue<uint32_t> freeQueue;
        };
    }
}