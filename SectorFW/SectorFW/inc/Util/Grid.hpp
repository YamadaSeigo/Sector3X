/*****************************************************************//**
 * @file   Grid.hpp
 * @brief グリッドを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <vector>

namespace SectorFW
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
}
