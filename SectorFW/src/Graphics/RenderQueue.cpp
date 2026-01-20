#include "Graphics/RenderQueue.h"

#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics
	{
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSessionExternal::AllocInstance(const InstanceData& inst) {
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

				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = inst;
			return InstanceIndex{ idx };
		}
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSessionExternal::AllocInstance(InstanceData&& inst)
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

				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = std::move(inst);
			return InstanceIndex{ idx };
		}
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSessionExternal::AllocInstance(const InstancePool& inst)
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

				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = inst;
			return InstanceIndex{ idx };
		}
		[[nodiscard]] InstanceIndex RenderQueue::ProducerSessionExternal::AllocInstance(InstancePool&& inst)
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

				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
			}
			rq->sharedInstanceArena->Data(slot)[idx] = std::move(inst);
			return InstanceIndex{ idx };
		}
		InstanceIndex RenderQueue::ProducerSessionExternal::NextInstanceIndex()
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

				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
			}
			return InstanceIndex{ idx };
		}
		void RenderQueue::ProducerSessionExternal::MemsetInstancePool(InstanceIndex index, const InstancePool& inst) noexcept
		{
			if (index.index < 0 || index.index >= rq->maxInstancesPerFrame) [[unlikely]] {
				LOG_ERROR("instance indexが正常な範囲ではありません (max {%d})", rq->maxInstancesPerFrame);
				return;
			}

			RebindIfNeeded();
			// 現在のフレームスロット
			const int slot = rq->current.load(std::memory_order_acquire);
			rq->sharedInstanceArena->Data(slot)[index.index] = inst;
		}


		size_t RenderQueue::ProducerSessionExternal::AllocInstancesFromWorldSoA(const Math::Matrix3x4fSoA& wSoa, InstanceIndex* outIdx)
		{
			const auto& W = wSoa;

			if (W.count == 0) return 0;

			// 入力ポインタ簡易チェック（全列必須）
			assert(W.m00 && W.m01 && W.m02 && W.tx &&
				W.m10 && W.m11 && W.m12 && W.ty &&
				W.m20 && W.m21 && W.m22 && W.tz && "WorldSoA has null pointer(s)");

			assert(W.count < (std::numeric_limits<uint32_t>::max)() && "overflow count of uint32_t!");

			RebindIfNeeded();

			const int slot = rq->current.load(std::memory_order_acquire); // 必ず ProducerSession のスロットに揃える
			auto& head = rq->sharedInstanceArena->head[slot];
			const uint32_t count = static_cast<uint32_t>(W.count);
			const uint32_t base = head.fetch_add(count, std::memory_order_acq_rel);
			auto* dst = rq->sharedInstanceArena->Data(slot) + base;

			// まとめて領域確保（飽和対策）
			const uint32_t cap = rq->maxInstancesPerFrame;
			const uint32_t remain = (base < cap) ? (cap - base) : 0u;
			const uint32_t toWrite = (std::min)(count, remain);

			if (toWrite == 0) {
				// 満杯
				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
				return 0;
			}
			if (toWrite < W.count) {
				// 入り切らなかった分がある（ログのみ）
				LOG_ERROR("インスタンスとして登録できなかった分があります {%d} -> {%d} (pool max {%d})",
					W.count, toWrite, rq->maxInstancesPerFrame);
			}

			// 1 行 = 4 要素を memcpy（アライン未保証でも安全）
			for (uint32_t i = 0; i < toWrite; ++i) {
				float r0[4] = { W.m00[i], W.m01[i], W.m02[i], W.tx[i] };
				float r1[4] = { W.m10[i], W.m11[i], W.m12[i], W.ty[i] };
				float r2[4] = { W.m20[i], W.m21[i], W.m22[i], W.tz[i] };

				std::memcpy(&dst[i].world.m[0][0], r0, sizeof(r0));
				std::memcpy(&dst[i].world.m[1][0], r1, sizeof(r1));
				std::memcpy(&dst[i].world.m[2][0], r2, sizeof(r2));

				if (outIdx) outIdx[i] = InstanceIndex{ base + i };
			}
			return toWrite;
		}

		size_t  RenderQueue::ProducerSessionExternal::AllocInstancesFromWorldSoAAndColorSoA(const Math::Matrix3x4fSoA& wSoa, const Math::Vec4f* cSoa, InstanceIndex* outIdx)
		{
			const auto& W = wSoa;

			if (W.count == 0) return 0;

			// 入力ポインタ簡易チェック（全列必須）
			assert(W.m00 && W.m01 && W.m02 && W.tx &&
				W.m10 && W.m11 && W.m12 && W.ty &&
				W.m20 && W.m21 && W.m22 && W.tz && "WorldSoA has null pointer(s)");

			assert(W.count < (std::numeric_limits<uint32_t>::max)() && "overflow count of uint32_t!");

			RebindIfNeeded();

			const int slot = rq->current.load(std::memory_order_acquire); // 必ず ProducerSession のスロットに揃える
			auto& head = rq->sharedInstanceArena->head[slot];
			const uint32_t count = static_cast<uint32_t>(W.count);
			const uint32_t base = head.fetch_add(count, std::memory_order_acq_rel);
			auto* dst = rq->sharedInstanceArena->Data(slot) + base;

			// まとめて領域確保（飽和対策）
			const uint32_t cap = rq->maxInstancesPerFrame;
			const uint32_t remain = (base < cap) ? (cap - base) : 0u;
			const uint32_t toWrite = (std::min)(count, remain);

			if (toWrite == 0) {
				// 満杯
				LOG_ERROR("一フレームの最大インスタンス数をオーバフローしました (max {%d})", rq->maxInstancesPerFrame);
				return 0;
			}
			if (toWrite < W.count) {
				// 入り切らなかった分がある（ログのみ）
				LOG_ERROR("インスタンスとして登録できなかった分があります {%d} -> {%d} (pool max {%d})",
					W.count, toWrite, rq->maxInstancesPerFrame);
			}

			// 1 行 = 4 要素を memcpy（アライン未保証でも安全）
			for (uint32_t i = 0; i < toWrite; ++i) {
				float r0[4] = { W.m00[i], W.m01[i], W.m02[i], W.tx[i] };
				float r1[4] = { W.m10[i], W.m11[i], W.m12[i], W.ty[i] };
				float r2[4] = { W.m20[i], W.m21[i], W.m22[i], W.tz[i] };

				std::memcpy(&dst[i].world.m[0][0], r0, sizeof(r0));
				std::memcpy(&dst[i].world.m[1][0], r1, sizeof(r1));
				std::memcpy(&dst[i].world.m[2][0], r2, sizeof(r2));

				// Color SoA から色をセット
				dst[i].color = cSoa[i];

				if (outIdx) outIdx[i] = InstanceIndex{ base + i };
			}
			return toWrite;
		}
	}

}


