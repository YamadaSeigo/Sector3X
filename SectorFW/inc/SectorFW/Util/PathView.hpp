/*****************************************************************//**
 * @file   PathView.hpp
 * @brief std::filesystem::pathのゼロコピービューを提供するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <filesystem>
#include <string_view>
#include <type_traits>

namespace SectorFW
{
	/**
	 * @brief std::filesystem::pathのゼロコピービューを提供するクラス
	 */
	class path_view {
	public:
		using path_type = std::filesystem::path;
		using value_type = typename path_type::value_type;
		using string_type = typename path_type::string_type;

		/**
		 * @brief 基になる文字列ビューの型
		 */
		using view_type = std::conditional_t<
			std::is_same_v<value_type, char>,
			std::string_view,
			std::wstring_view
		>;
		/**
		 * @brief デフォルトコンストラクタ (空のビューを作成)
		 */
		path_view() = default;

		/**
		 * @brief std::filesystem::pathから構築
		 * @param path 元となるパス
		 */
		explicit path_view(const path_type& path)
			: view_(path.native().data(), path.native().size()) {
		}
		/**
		 * @brief 文字列から構築
		 * @param str 元となる文字列
		 */
		explicit path_view(const string_type& str)
			: view_(str.data(), str.size()) {
		}
		/**
		 * @brief ポインタと長さから構築
		 * @param ptr 元となる文字列のポインタ
		 * @param len 文字列の長さ
		 */
		path_view(const value_type* ptr, size_t len)
			: view_(ptr, len) {
		}
		/**
		 * @brief 基になる文字列ビューを取得
		 * @return view_type 基になる文字列ビュー
		 */
		view_type view() const noexcept { return view_; }
		/**
		 * @brief 基になる文字列ビューへの暗黙的変換
		 */
		operator view_type() const noexcept { return view_; }
		/**
		 * @brief 基になる文字列データへのポインタを取得
		 * @return const value_type* 文字列データへのポインタ
		 */
		const value_type* data() const noexcept { return view_.data(); }
		/**
		 * @brief 基になる文字列の長さを取得
		 * @return size_t 文字列の長さ
		 */
		size_t size() const noexcept { return view_.size(); }
		/**
		 * @brief ビューが空かどうかを判定
		 * @return bool 空ならtrue、そうでなければfalse
		 */
		bool empty() const noexcept { return view_.empty(); }
		/**
		 * @brief std::filesystem::pathに変換
		 * @return path_type std::filesystem::pathオブジェクト
		 */
		path_type to_path() const { return path_type(view_); }
	private:
		view_type view_{};
	};
}