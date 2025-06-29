/*****************************************************************//**
 * @file   Flatten.h
 * @brief タプルを再帰的に展開するヘッダファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <tuple>
#include <type_traits>

namespace SectorFW
{
    /**
	 * @brief タプルを再帰的に展開するためのヘルパー構造体
     */
    template<typename T, typename = void>
    struct ValueOrSelfHelper {
        using type = T;
    };
    /**
	 * @brief タプル型を持つ場合のヘルパー構造体
     */
    template<typename T>
    struct ValueOrSelfHelper<T, std::void_t<typename T::tuple_type>> {
        using type = typename T::tuple_type;
    };
    /**
	 * @brief タプルを再帰的に展開するためのエイリアス
     */
    template<typename T>
    using ValueOrSelf = typename ValueOrSelfHelper<T>::type;
    /**
	 * @brief タプルを再帰的に展開するための構造体
     */
    template<typename T>
    struct Flatten {
        using type = std::tuple<T>;
    };
    /**
	 * @brief タプルを再帰的に展開するための特殊化
     */
    template<typename... Ts>
    struct Flatten<std::tuple<Ts...>> {
    private:
        /**
		 * @brief タプルの要素を再帰的に展開するためのヘルパー型
         */
        template<typename T>
        using flatten_t = typename Flatten<ValueOrSelf<T>>::type;
        /**
		* @brief タプルを結合するためのヘルパー構造体
        */
        template<typename... Tuples>
        struct TupleConcat;
        /**
		 * @brief タプルを結合するための特殊化
         */
        template<typename... T1s, typename... T2s>
        struct TupleConcat<std::tuple<T1s...>, std::tuple<T2s...>> {
            using type = std::tuple<T1s..., T2s...>;
        };
        /**
		 * @brief タプルを再帰的に結合するための特殊化
         */
        template<typename T1, typename T2, typename... Rest>
        struct TupleConcat<T1, T2, Rest...> {
            using type = typename TupleConcat<typename TupleConcat<T1, T2>::type, Rest...>::type;
        };
    public:
        /**
		 * @brief 再帰的に展開されたタプルの型を定義する
         */
        using type = typename TupleConcat<flatten_t<Ts>...>::type;
    };
    /**
	 * @brief FlattenTは、与えられた型Tを再帰的に展開し、最終的な型を取得するためのエイリアス
     */
    template<typename T>
    using FlattenT = typename Flatten<ValueOrSelf<T>>::type;
}
