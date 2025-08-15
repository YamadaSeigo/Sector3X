/*****************************************************************//**
 * @file   extract_type.hpp
 * @brief 型を引数から抽出するためのユーティリティ関数
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <optional>
#include <type_traits>
#include <vector>

namespace SectorFW
{
	/**
	 * @brief 型を引数から抽出するためのユーティリティ関数
	 *
	 * これらの関数は、可変長引数テンプレートを使用して、特定の型の最初のインスタンスやすべてのインスタンスを抽出します。
	 * また、必須の型が見つからない場合はコンパイル時エラーを発生させます。
	 */
	template<typename T>
	std::optional<T> extract_first_of_type() noexcept {
		return std::nullopt;
	}
	/**
	 * @brief 可変長引数から最初の指定された型のインスタンスを抽出します。
	 * @tparam T 抽出する型
	 * @param first 最初の引数
	 * @param rest 残りの引数
	 * @return 指定された型のインスタンスが見つかった場合はstd::optional<T>、見つからない場合はstd::nullopt
	 */
	template<typename T, typename First, typename... Rest>
	std::optional<T> extract_first_of_type(First&& first, Rest&&... rest) noexcept {
		if constexpr (std::is_same_v<std::decay_t<First>, T>) {
			return T(std::forward<First>(first));
		}
		else {
			return extract_first_of_type<T>(std::forward<Rest>(rest)...);
		}
	}

	/**
	 * @brief 可変長引数からすべての指定された型のインスタンスを抽出します。
	 * @return 抽出された型のインスタンスのベクター
	 */
	template<typename T>
	std::vector<T> extract_all_of_type() {
		return {};
	}

	/**
	 * @brief 可変長引数からすべての指定された型のインスタンスを抽出します。
	 * @param first 最初の引数
	 * @param ...rest 残りの引数
	 * @return 抽出された型のインスタンスのベクター
	 */
	template<typename T, typename First, typename... Rest>
	std::vector<T> extract_all_of_type(First&& first, Rest&&... rest) {
		std::vector<T> result = extract_all_of_type<T>(std::forward<Rest>(rest)...);
		if constexpr (std::is_same_v<std::decay_t<First>, T>) {
			result.insert(result.begin(), T(std::forward<First>(first)));
		}
		return result;
	}
	/**
	 * @brief 可変長引数から必須の型を抽出します。
	 * @return 抽出された型のインスタンス
	 */
	template<typename T>
	T extract_required_type() noexcept {
		static_assert(sizeof(T) == 0, "No matching type in arguments!");
	}
	/**
	 * @brief 可変長引数から必須の型を抽出します。
	 * @param first 最初の引数
	 * @param ...rest 残りの引数
	 * @return 抽出された型のインスタンス
	 */
	template<typename T, typename First, typename... Rest>
	T extract_required_type(First&& first, Rest&&... rest) noexcept {
		if constexpr (std::is_same_v<std::decay_t<First>, T>) {
			return T(std::forward<First>(first));
		}
		else {
			return extract_required_type<T>(std::forward<Rest>(rest)...);
		}
	}
}