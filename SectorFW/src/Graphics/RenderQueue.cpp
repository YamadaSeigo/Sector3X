#include "Graphics/RenderQueue.h"

#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics
	{
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSession::AllocInstance(const InstanceData& inst) {
            assert(inst.worldMtx.m[3][3] == 1.0f && "InstanceData.worldMtx should be affine matrix.");

			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->sharedInstanceArena->head[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = inst;
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSession::AllocInstance(InstanceData&& inst)
		{
			assert(inst.worldMtx.m[3][3] == 1.0f && "InstanceData.worldMtx should be affine matrix.");

			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->sharedInstanceArena->head[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = std::move(inst);
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSession::AllocInstance(const InstancePool& inst)
		{
			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->sharedInstanceArena->head[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = inst;
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSession::AllocInstance(InstancePool&& inst)
		{
			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->sharedInstanceArena->head[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = std::move(inst);
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSession::NextInstanceIndex()
		{
			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			auto& pos = rq->sharedInstanceArena->head[slot];
			uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
			// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
			if (idx >= rq->maxInstancesPerFrame) [[unlikely]] {
				// 飽和させるか、エラー処理
				idx = rq->maxInstancesPerFrame - 1;

				LOG_ERROR("RenderQueue::ProducerSession::AllocInstance: instance pool overflow (max {})", rq->maxInstancesPerFrame);
			}
			return InstanceIndex{ idx };
		}
		void RenderQueue::ProducerSession::MemsetInstancePool(InstanceIndex index, const InstancePool& inst) noexcept
		{
			if (index.index < 0 || index.index >= rq->maxInstancesPerFrame) [[unlikely]] {
				LOG_ERROR("RenderQueue::ProducerSession::MemsetInstancePool: instance index out of range (max {})", rq->maxInstancesPerFrame);
				return;
			}

			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			rq->sharedInstanceArena->Data(slot)[index.index] = inst;
		}
	}
}