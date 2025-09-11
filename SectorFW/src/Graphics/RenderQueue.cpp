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
			if (idx >= rq->maxInstancesPerFrame) {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->instancePools[slot][idx] = inst;
			return InstanceIndex{ idx };
		}
	}
}