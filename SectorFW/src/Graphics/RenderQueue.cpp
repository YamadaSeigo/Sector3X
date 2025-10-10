#include "Graphics/RenderQueue.h"

#include "Debug/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSession::AllocInstance(const InstanceData& inst) {
			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->instWritePos[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->instancePools[slot][idx] = inst;
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSession::AllocInstance(InstanceData&& inst)
		{
			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->instWritePos[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->instancePools[slot][idx] = std::move(inst);
			return InstanceIndex{ idx };
		}

        size_t RenderQueue::ProducerSession::AllocInstances(
            const InstanceData* src, size_t count, InstanceIndex* outIndices)
        {
            if (!src || !outIndices || count == 0) return 0;

            RebindIfNeeded();

            // 現在のフレームスロット
            const int slot = rq->current.load(std::memory_order_acquire);
            auto& pos = rq->instWritePos[slot];

            // まず "count" 分の連番領域を atomically 予約
            const uint32_t base = pos.fetch_add(static_cast<uint32_t>(count), std::memory_order_acq_rel);

            // 残キャパ計算とクランプ
            const uint32_t max = rq->maxInstancesPerFrame;
            const uint32_t avail = (base < max) ? (max - base) : 0u;
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(count, avail));

            if (n == 0) [[unlikely]] {
                LOG_ERROR("RenderQueue::ProducerSession::AllocInstances: instance pool overflow (max {}, base {}, req {})",
                    max, base, count);
                // fetch_add 済みの分は戻さない方針（他スレッドとの競合簡略化）。必要なら設計で「予約失敗時ロールバック」を検討
                return 0;
            }

            // 登録先
            InstanceData* __restrict dst = rq->instancePools[slot].get() + base;

            // 連続コピー（trivially copyable なら memcpy で一括）
            if constexpr (std::is_trivially_copyable_v<InstanceData>) {
                std::memcpy(dst, src, sizeof(InstanceData) * n);
            }
            else {
                for (uint32_t i = 0; i < n; ++i) {
                    dst[i] = src[i]; // コピー
                }
            }

            // 返却インデックス（連番）を一気に書き込み
            // InstanceIndex が uint32_t から構築可能（{idx} で可）である前提
            for (uint32_t i = 0; i < n; ++i) {
                outIndices[i] = InstanceIndex{ base + i };
            }

            // もし要求数 > 供給数ならログ
            if (n < count) [[unlikely]] {
                LOG_WARNING("RenderQueue::ProducerSession::AllocInstances: clamped {} -> {} due to capacity (max {}).",
                    count, n, max);
            }

            return n;
        }
	}
}