/*****************************************************************//**
 * @file   TypeChecker.hpp
 * @brief 型チェックとコンセプトを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <type_traits>
#include <utility>

namespace SectorFW
{
	// 任意のテンプレート引数数に対応するCRTP継承判定
	template <typename T, template <typename, typename...> class CRTP>
	struct is_crtp_base_of {
	private:
		// どんな追加引数...でもCRTP<T, ...>と一致すればOK
		template <typename U>
		static auto test(CRTP<U, std::decay_t<T> >*) -> std::true_type {}

		template <typename U, typename... Args>
		static auto test(CRTP<U, Args...>*) -> std::true_type {}

		static auto test(...) -> std::false_type {}

	public:
		static constexpr bool value = decltype(test<T>(std::declval<T*>()))::value;
	};

	// CRTP<T> に対応した1引数用判定
	template <typename T, template <typename> class CRTP>
	concept is_crtp_base_of_1arg = requires(T * t) {
		static_cast<CRTP<T>*>(t);
	};

	template <typename T, template <typename, typename...> class CRTP>
	constexpr bool is_crtp_base_of_v = is_crtp_base_of<T, CRTP>::value;

	// ユーザー拡張点：デフォルトは false
	template<class T> struct user_primitive : std::false_type {};
	template<class T>
	inline constexpr bool user_primitive_v = user_primitive<std::remove_cv_t<T>>::value;

	/**
	 * @brief プリミティブ型をチェックするコンセプト
	 */
	template<typename T>
	concept IsPrimitive = std::is_arithmetic_v<T> || std::is_enum_v<T> || user_primitive_v<T>;
	/**
	 * @brief 型 T が AllowedTypes... のいずれかと一致するかをチェックするコンセプト
	 */
	template<typename T, typename... AllowedTypes>
	concept OneOf = (std::is_same_v<T, AllowedTypes> || ...);
	/**
	 * @brief QueryTypes のすべてが AllowedTypes... に含まれているかをチェックするコンセプト
	 */
	template<typename... QueryTypes, typename... AllowedTypes>
	concept AllOf = (OneOf<QueryTypes, AllowedTypes...> && ...);

	template<typename T>
	concept PointerType = std::is_pointer_v<T>;
}