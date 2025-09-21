/*****************************************************************//**
 * @file   SpscRing.hpp
 * @brief シングルプロデューサ・シングルコンシューマのリングバッファ
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <vector>
#include <atomic>
#include <optional>

namespace SectorFW
{
	/**
	 * @brief シングルプロデューサ・シングルコンシューマのリングバッファ
	 */
	template<class T>
	class SpscRing {
	public:
		/**
		 * @brief コンストラクタ
		 * @param capacity_pow2 バッファの容量（2のべき乗）
		 */
		explicit SpscRing(size_t capacity_pow2 = 1024)
			: m_mask(capacity_pow2 - 1),
			m_buf(capacity_pow2) {
		}
		/**
		 * @brief 要素をバッファに追加する関数
		 * @param v 追加する要素
		 * @return bool 追加に成功したかどうか
		 */
		bool push(const T& v) {
			auto head = m_head.load(std::memory_order_relaxed);
			auto next = (head + 1) & m_mask;
			if (next == m_tail.load(std::memory_order_acquire)) return false; // full
			m_buf[head] = v;
			m_head.store(next, std::memory_order_release);
			return true;
		}
		/**
		 * @brief 要素をバッファから取り出す関数
		 * @return std::optional<T> 取り出した要素（空の場合はstd::nullopt）
		 */
		std::optional<T> pop() {
			auto tail = m_tail.load(std::memory_order_relaxed);
			if (tail == m_head.load(std::memory_order_acquire)) return std::nullopt; // empty
			T v = std::move(m_buf[tail]);
			m_tail.store((tail + 1) & m_mask, std::memory_order_release);
			return v;
		}
		/**
		 * @brief バッファが空かどうかを確認する関数
		 * @return bool 空の場合はtrue、そうでない場合はfalse
		 */
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