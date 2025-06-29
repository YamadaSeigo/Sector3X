/*****************************************************************//**
 * @file   TypeChecker.hpp
 * @brief 型チェックとコンセプトを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <type_traits>

namespace SectorFW
{
     /**
     * @brief 任意の型Argと組み合わせて Base<T, Arg> を探す
     */
    template <typename T, template <typename, typename> class BaseTemplate>
    struct is_crtp_base_of {
    private:
        template <typename Arg>
        static std::true_type test(BaseTemplate<T, Arg>*) {}

        static std::false_type test(...) {}

    public:
        static constexpr bool value = decltype(test(std::declval<T*>()))::value;
    };
    /**
	 * @brief プリミティブ型をチェックするコンセプト
     */
    template<typename T>
    concept IsPrimitive = std::is_arithmetic_v<T> || std::is_enum_v<T>;
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
}
