// SpscRing.h
#pragma once
#include <vector>
#include <atomic>
#include <optional>

namespace SectorFW
{
	template<class T>
	class SpscRing {
	public:
		explicit SpscRing(size_t capacity_pow2 = 1024)
			: m_mask(capacity_pow2 - 1),
			m_buf(capacity_pow2) {
		}

		bool push(const T& v) {
			auto head = m_head.load(std::memory_order_relaxed);
			auto next = (head + 1) & m_mask;
			if (next == m_tail.load(std::memory_order_acquire)) return false; // full
			m_buf[head] = v;
			m_head.store(next, std::memory_order_release);
			return true;
		}

		std::optional<T> pop() {
			auto tail = m_tail.load(std::memory_order_relaxed);
			if (tail == m_head.load(std::memory_order_acquire)) return std::nullopt; // empty
			T v = std::move(m_buf[tail]);
			m_tail.store((tail + 1) & m_mask, std::memory_order_release);
			return v;
		}

		bool empty() const {
			return m_tail.load(std::memory_order_acquire) ==
				m_head.load(std::memory_order_acquire);
		}

	private:
		const size_t m_mask;
		std::vector<T> m_buf;
		std::atomic<size_t> m_head{ 0 };
		std::atomic<size_t> m_tail{ 0 };
	};
}