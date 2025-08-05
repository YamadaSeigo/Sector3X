#pragma once

#include <atomic>

namespace SectorFW
{
	template<typename T>
	struct CopyableAtomic {
		std::atomic<T> value;

		CopyableAtomic() noexcept = default;
		CopyableAtomic(T v) noexcept : value(v) {}

		// コピー時はデフォルト初期化（0）
		CopyableAtomic(const CopyableAtomic&) noexcept : value(0) {}
		CopyableAtomic& operator=(const CopyableAtomic&) noexcept {
			value.store(0);
			return *this;
		}

		// 明示的にatomic操作を委譲
		inline T load(std::memory_order order = std::memory_order_seq_cst) const {
			return value.load(order);
		}

		inline void store(T v, std::memory_order order = std::memory_order_seq_cst) {
			value.store(v, order);
		}

		inline T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) {
			return value.fetch_add(arg, order);
		}

		inline T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) {
			return value.fetch_sub(arg, order);
		}
	};

}
