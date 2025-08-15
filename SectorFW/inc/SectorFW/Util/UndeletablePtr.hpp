/*****************************************************************//**
 * @file   UndeletablePtr.hpp
 * @brief deleteを防ぐためのポインタラッパークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

namespace SectorFW
{
	/**
	 * @brief deleteを防ぐためのポインタラッパークラス
	 */
	template<typename T>
	class UndeletablePtr {
		/**
		 * @brief 本体
		 */
		T* ptr;
	public:
		/**
		 * @brief コンストラクタ
		 * @param p ポインタ
		 */
		explicit UndeletablePtr(T* p) noexcept : ptr(p) {}
		/**
		 * @brief　実体化
		 * @return T* ポインタ
		 */
		T* operator->() const noexcept { return ptr; }
		/**
		 * @brief 参照演算子
		 * @return T& 参照
		 */
		T& operator*()  const noexcept { return *ptr; }
		/**
		 * @brief ポインタを取得する関数
		 * @return T* ポインタ
		 */
		T* get() const noexcept { return ptr; }

		/**
		 * @brief  deleteを防ぐため、明示的なdeleteを禁止
		 */
		void operator delete(void*) = delete;

		/**
		 * @brief ポインタが有効かどうか
		 * @return 有効ならtrue
		 */
		bool IsValid() const noexcept { return ptr != nullptr; }
	};
}