/*****************************************************************//**
 * @file   EntityIDAllocator.h
 * @brief エンティティIDを管理するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

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
		/**
		 * @brief エンティティIDを管理するクラス(スレッドセーフ)
		 */
		class EntityIDAllocator {
		public:
			/**
			 * @brief コンストラクタ
			 * @param maxEntities 最大エンティティ数
			 */
			explicit EntityIDAllocator(size_t maxEntities)
				: maxEntities(static_cast<uint32_t>(maxEntities)),
				generations(maxEntities),
				nextIndex(0),
				freeQueue(static_cast<int>(maxEntities))
			{
				// Initially empty
			}
			/**
			 * @brief 新しいエンティティIDを作成する関数
			 * @return EntityID 新しいエンティティID
			 */
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
			/**
			 * @brief エンティティIDを破棄する関数
			 * @param id 破棄するエンティティID
			 */
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
			/**
			 * @brief エンティティIDが有効かどうかを確認する関数
			 * @param id エンティティID
			 * @return bool 有効な場合はtrue、無効な場合はfalse
			 */
			bool IsAlive(EntityID id) const noexcept {
				if (id.index >= maxEntities) return false;
				return generations[id.index].load(std::memory_order_acquire) == id.generation;
			}
			/**
			 * @brief エンティティIDの最大数を取得する関数
			 * @return uint32_t 最大エンティティ数
			 */
			uint32_t Capacity() const noexcept { return maxEntities; }

		private:
			/**
			 * @brief 最大エンティティ数
			 */
			const uint32_t maxEntities;
			/**
			 * @brief 次のエンティティIDのインデックス
			 */
			std::atomic<uint32_t> nextIndex;
			/**
			 * @brief 世代管理のための配列
			 */
			std::vector<std::atomic<uint32_t>> generations;
			/**
			 * @brief 未使用のエンティティIDを管理するキュー
			 */
			moodycamel::ConcurrentQueue<uint32_t> freeQueue;
		};
	}
}