/*****************************************************************//**
 * @file   Grid.hpp
 * @brief グリッドを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <vector>

namespace SFW
{
	/**
	 * @brief 2Dグリッドを表すクラス
	 * @tparam T グリッドの要素の型
	 * @tparam Size サイズの型（デフォルトはsize_t）
	 */
	template<typename T, typename Size = size_t>
	class Grid2D {
	public:
		/**
		 * @brief デフォルトコンストラクタ
		 * @param width グリッドの幅
		 * @param height グリッドの高さ
		 */
		explicit Grid2D(Size width, Size height) noexcept
			: m_width(width), m_height(height), m_data(width* height) {
		}

		/**
		 * @brief 引数付きコンストラクタ
		 * @tparam Args 可変長テンプレート引数
		 * @param width グリッドの幅
		 * @param height グリッドの高さ
		 * @param args 要素の初期化に使用する引数
		 */
		template<typename... Args>
		Grid2D(Size width, Size height, Args&&... args) noexcept
			: m_width(width), m_height(height), m_data(width * height, T(std::forward<Args>(args)...)) {
		}

		/**
		 * @brief グリッドの要素にアクセスする演算子オーバーロード
		 * @param x グリッドのx座標
		 * @param y グリッドのy座標
		 * @return 要素への参照
		 */
		T& operator()(Size x, Size y) {
			return m_data[y * m_width + x];
		}
		/**
		 * @brief グリッドの要素にアクセスする演算子オーバーロード（const版）
		 * @param x グリッドのx座標
		 * @param y グリッドのy座標
		 * @return 要素への参照
		 */
		const T& operator()(Size x, Size y) const {
			return m_data[y * m_width + x];
		}
		/**
		 * @brief グリッドの幅を取得する関数
		 * @return グリッドの幅
		 */
		Size width() const noexcept { return m_width; }
		/**
		 * @brief グリッドの高さを取得する関数
		 * @return グリッドの高さ
		 */
		Size height() const noexcept { return m_height; }
		/**
		 * @brief グリッドの要素をイテレートするためのbeginとend関数
		 * @return イテレータのbeginとend
		 */
		auto begin() noexcept { return m_data.begin(); }
		/**
		 * @brief グリッドの要素をイテレートするためのend関数
		 * @return イテレータのend
		 */
		auto end() noexcept { return m_data.end(); }
		/**
		 * @brief グリッドの要素をイテレートするためのconst版beginとend関数
		 * @return イテレータのbeginとend
		 */
		auto begin() const noexcept { return m_data.begin(); }
		/**
		 * @brief グリッドの要素をイテレートするためのconst版end関数
		 * @return イテレータのend
		 */
		auto end() const noexcept { return m_data.end(); }
		/**
		 * @brief グリッドのサイズを取得する関数
		 * @return グリッドのサイズ（幅 * 高さ）
		 */
		Size size() const noexcept { return m_width * m_height; }
	private:
		/**
		 * @brief グリッドの幅と高さ
		 */
		Size m_width, m_height;
		/**
		 * @brief グリッドの要素を格納するベクター
		 */
		std::vector<T> m_data;
	};

	/**
	* @brief 3Dグリッドを表すクラス
	* @tparam T   グリッドの要素の型
	* @tparam Size サイズの型（デフォルトはsize_t）
	*/
	template<typename T, typename Size = size_t>
	class Grid3D {
	public:
		/**
		 * @brief コンストラクタ
		 * @param width  グリッドの幅 (x)
		 * @param height グリッドの高さ (y)
		 * @param depth  グリッドの奥行 (z)
		 */
		explicit Grid3D(Size width, Size height, Size depth) noexcept
			: m_width(width), m_height(height), m_depth(depth),
			m_data(static_cast<size_t>(width)* static_cast<size_t>(height)* static_cast<size_t>(depth)) {
		}

		/**
		 * @brief 引数付きコンストラクタ
		 * @tparam Args 可変長テンプレート引数
		 * @param width  グリッドの幅 (x)
		 * @param height グリッドの高さ (y)
		 * @param depth  グリッドの奥行 (z)
		 * @param args   要素の初期化に使用する引数
		 */
		template<typename... Args>
		Grid3D(Size width, Size height, Size depth, Args&&... args) noexcept
			: m_width(width), m_height(height), m_depth(depth),
			m_data(static_cast<size_t>(width)* static_cast<size_t>(height)* static_cast<size_t>(depth), T(std::forward<Args>(args)...)) {
		}

		/**
		 * @brief 要素アクセス（非const）
		 * @param x x座標
		 * @param y y座標
		 * @param z z座標
		 * @return 要素への参照
		 */
		T& operator()(Size x, Size y, Size z) {
			return m_data[indexOf(x, y, z)];
		}

		/**
		 * @brief 要素アクセス（const）
		 * @param x x座標
		 * @param y y座標
		 * @param z z座標
		 * @return 要素への参照
		 */
		const T& operator()(Size x, Size y, Size z) const {
			return m_data[indexOf(x, y, z)];
		}

		/// 幅 (x)
		Size width()  const noexcept { return m_width; }
		/// 高さ (y)
		Size height() const noexcept { return m_height; }
		/// 奥行 (z)
		Size depth()  const noexcept { return m_depth; }

		/// 総要素数 = width * height * depth
		Size size() const noexcept { return m_width * m_height * m_depth; }

		/// イテレータ（非const）
		auto begin() noexcept { return m_data.begin(); }
		auto end()   noexcept { return m_data.end(); }

		/// イテレータ（const）
		auto begin() const noexcept { return m_data.begin(); }
		auto end()   const noexcept { return m_data.end(); }

		/// データ先頭ポインタ（必要なら）
		T* data() noexcept { return m_data.data(); }
		const T* data() const noexcept { return m_data.data(); }

	private:
		// 一次元インデックス計算：z*(w*h) + y*w + x
		size_t indexOf(Size x, Size y, Size z) const noexcept {
			return static_cast<size_t>(z) * static_cast<size_t>(m_width) * static_cast<size_t>(m_height)
				+ static_cast<size_t>(y) * static_cast<size_t>(m_width)
				+ static_cast<size_t>(x);
		}

		Size m_width, m_height, m_depth;
		std::vector<T> m_data;
	};
}