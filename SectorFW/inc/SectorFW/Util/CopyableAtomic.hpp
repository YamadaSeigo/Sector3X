/*****************************************************************//**
 * @file   CopyableAtomic.hpp
 * @brief コピー可能なatomicラッパークラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <atomic>

namespace SFW
{
	/**
	 * @brief コピー可能なatomicラッパークラス
	 */
	template<typename T>
	struct CopyableAtomic {
		std::atomic<T> value;

		/**
		 * @brief デフォルトコンストラクタ
		 */
		CopyableAtomic() noexcept = default;
		/**
		 * @brief 値を指定して初期化するコンストラクタ
		 * @param v 初期化する値
		 */
		CopyableAtomic(T v) noexcept : value(v) {}

		/**
		 * @brief コピー時はデフォルト初期化（0）
		 * @param other コピー元
		 * @return CopyableAtomic& 自身の参照
		 */
		CopyableAtomic(const CopyableAtomic&) noexcept : value(0) {}
		CopyableAtomic& operator=(const CopyableAtomic&) noexcept {
			value.store(0);
			return *this;
		}

		/**
		 * @brief 明示的にatomic操作を委譲
		 * @param order メモリオーダー
		 * @return T ロードした値
		 */
		inline T load(std::memory_order order = std::memory_order_seq_cst) const {
			return value.load(order);
		}
		/**
		 * @brief ストア操作を委譲
		 * @param v 格納する値
		 * @param order メモリオーダー
		 */
		inline void store(T v, std::memory_order order = std::memory_order_seq_cst) {
			value.store(v, order);
		}
		/**
		 * @brief 加算操作を委譲
		 * @param arg 加算する値
		 * @param order メモリオーダー
		 * @return T 加算前の値
		 */
		inline T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) {
			return value.fetch_add(arg, order);
		}
		/**
		 * @brief 減算操作を委譲
		 * @param arg 減算する値
		 * @param order メモリオーダー
		 * @return T 減算前の値
		 */
		inline T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) {
			return value.fetch_sub(arg, order);
		}
	};
}