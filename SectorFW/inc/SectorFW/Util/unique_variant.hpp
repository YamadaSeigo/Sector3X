/*****************************************************************//**
 * @file   unique_variant.hpp
 * @brief ユニークな型のvariantを生成するためのヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <variant>
#include <type_traits>
#include <tuple>

namespace SFW
{
	/**
	 * @brief 指定された型Tが、可変長引数の型リストTs...のいずれかと同じかどうかを判定する
	 */
	template <typename T, typename... Ts>
	inline constexpr bool is_in_v = (std::is_same_v<T, Ts> || ...);
	/**
	 * @brief ユニークな型のリストを生成するためのヘルパー構造体
	 */
	template <typename... Ts>
	struct unique_types {
	private:
		/**
		 * @brief 再帰的にユニークな型を抽出するための内部実装
		 */
		template <typename... Us>
		struct impl;
		/**
		 * @brief 空のタプルを生成する特化版
		 */
		template <>
		struct impl<> {
			using type = std::tuple<>;
		};
		/**
		 * @brief ユニークな型を抽出するための再帰的な実装
		 */
		template <typename T, typename... Us>
		struct impl<T, Us...> {
			using tail = typename impl<Us...>::type;
			using type = std::conditional_t<
				is_in_v<T, Us...>,
				tail,
				decltype(std::tuple_cat(std::declval<std::tuple<T>>(), std::declval<tail>()))
			>;
		};
	public:
		/**
		 * @brief ユニークな型のタプルを生成する
		 */
		using type = typename impl<Ts...>::type;
	};
	/**
	 * @brief tuple → variant に変換
	 */
	template <typename Tuple>
	struct tuple_to_variant;
	/**
	 * @brief tuple → variant に変換する特化版
	 */
	template <typename... Ts>
	struct tuple_to_variant<std::tuple<Ts...>> {
		using type = std::variant<Ts...>;
	};
	/**
	 * @brief unique_variant：Ts... でも tuple<> でもOKなオーバーロード
	 */
	template <typename T>
	struct unique_variant_impl;
	/**
	 * @brief unique_variant_impl：tuple<Ts...> の特化版
	 */
	template <typename... Ts>
	struct unique_variant_impl<std::tuple<Ts...>> {
		using type = typename tuple_to_variant<typename unique_types<Ts...>::type>::type;
	};
	/**
	 * @brief unique_variant_impl_alt：可変長引数テンプレートを使用してユニークな型のvariantを生成する
	 */
	template <typename... Ts>
	struct unique_variant_impl_alt {
		using type = typename tuple_to_variant<typename unique_types<Ts...>::type>::type;
	};
	/**
	 * @brief ユニークな型のvariantを生成するためのエイリアス(tuple版)
	 */
	template <typename T>
	using unique_variant_from_tuple = typename unique_variant_impl<T>::type;
	/**
	 * @brief ユニークな型のvariantを生成するためのエイリアス（可変長引数テンプレート版）
	 */
	template <typename... Ts>
	using unique_variant_from_args = typename unique_variant_impl_alt<Ts...>::type;
}