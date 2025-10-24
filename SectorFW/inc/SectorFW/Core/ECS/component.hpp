/*****************************************************************//**
 * @file   component.hpp
 * @brief  コンポーネントの定義と関連するユーティリティを提供するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <functional>
#include <bitset>
#include <type_traits>

#include "../../Util/Flatten.hpp"

#include "../../Util/for_each_macro.hpp"
#include "../../Util/unique_variant.hpp"
#include "../../Util/TypeChecker.hpp"

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief 最大コンポーネント数を定義する定数
		 */
#ifdef MAX_COMPONENTS_NUM
		constexpr size_t MaxComponents = MAX_COMPONENTS_NUM;
#else
		 /**
		  * @brief 最大コンポーネント数を定義する定数
		  */
		constexpr size_t MaxComponents = 64;
#endif
		/**
		 * @brief コンポーネントの型IDを定義する型
		 */
		using ComponentTypeID = uint32_t;
		/**
		 * @brief コンポーネントマスクを定義するビットセット型
		 */
		using ComponentMask = std::bitset<MaxComponents>;
		/**
		 * @brief コンポーネントのレイアウトを定義する構造体
		 */
		struct ComponentInfo {
			size_t offset;
			size_t stride;
		};
		/**
		 * @brief まばらなコンポーネントを識別するタグ
		 */
		struct SparseComponentTag {};
		/**
		 * @brief まばらなコンポーネントを識別するためのトレイト
		 */
		template<typename, typename = void>
		struct is_sparse_component : std::false_type {};
		/**
		 * @brief　まばらなコンポーネントを識別するためのトレイトの特殊化
		 */
		template<typename T>
		struct is_sparse_component<T, std::void_t<typename T::sparse_tag>>
			: std::is_same<typename T::sparse_tag, SparseComponentTag> {
		};
		/**
		 * @brief まばらなコンポーネントを識別するためのトレイトの値
		 */
		template<typename T>
		constexpr bool is_sparse_component_v = is_sparse_component<T>::value;
		/**
		 * @brief SoAコンポーネントを識別するためのトレイト
		 */
		template<typename T, typename = void>
		struct is_soa_component : std::false_type {};
		/**
		 * @brief SoAコンポーネントを識別するためのトレイトの特殊化
		 */
		template<typename T>
		struct is_soa_component<T, std::void_t<typename T::soa_type>> : std::true_type {};
		/**
		 * @brief SoAコンポーネントを識別するためのトレイトの値
		 */
		template<typename T>
		constexpr bool is_soa_component_v = is_soa_component<T>::value;
		/**
		 * @brief まばらなコンポーネントを識別するためのコンセプト
		 */
		template <typename T>
		concept SparseComponent = is_sparse_component_v<T>;
		/**
		 * @brief SoAコンポーネントを識別するためのコンセプト
		 */
		template<typename T>
		concept IsSoAComponent = requires {
			typename T::soa_type;					 // T::soa_type が存在する
			typename T::variant_type;                // T::variant_type が存在する
			{ T::member_ptr_tuple } -> std::convertible_to<decltype(T::member_ptr_tuple)>; // 静的メンバ変数として存在
		};
		/**
		 * @brief SoAコンポーネントのポインタ型を定義するテンプレート
		 */
		template<typename T, typename = void>
		struct SoAPtr {
			using type = T*;
		};
		/**
		 * @brief SoAコンポーネントのポインタ型を定義するテンプレートの特殊化
		 */
		template<typename T>
			requires IsSoAComponent<T>
		struct SoAPtr<T> {
			using type = typename T::ToPtr;
		};
		/**
		 * @brief T がポインタなら const T::element_type*、そうでなければ const T
		 */
		template<typename T>
		using ConstReturnType = std::conditional_t<
			std::is_pointer_v<T>,
			std::add_pointer_t<std::add_const_t<std::remove_pointer_t<T>>>,
			std::add_const_t<T>
		>;

		//まばらなコンポーネントを識別するためのタグを定義するマクロ
#define SPARSE_TAG using sparse_tag = SFW::ECS::SparseComponentTag;
		//メンバ変数を定義するマクロ
#define WRAP_MEMBER(var,name) &name::var
		//メンバ変数のポインタを定義するマクロ
#define WRAP_MEMBER_PTR(var,name) &name::p_##var
		//渡された引数をコンマで区切って展開するマクロ
#define WRAP_MEMBER_FOREACH(className, macro, ...) FOR_EACH_ARG(macro, className,COMMA,__VA_ARGS__)
		//引数をdecltypeでラップするマクロ
#define WRAP_DECLTYPE(x) decltype(x)
		//引数をSoAPtrでラップしてポインタ型を取得するマクロ
#define WRAP_DECLTYPE_PTR(x) SFW::ECS::SoAPtr<decltype(x)>::type p_##x
		//コンポーネントのメンバーを取得するため関数定義マクロ
#define DEFINE_GET_FUNCTION(x) inline decltype(p_##x) x##() noexcept {return p_##x;} inline SFW::ECS::ConstReturnType<decltype(p_##x)> x##() const noexcept {return p_##x;}
		//SoAコンポーネントの定義マクロ
#define DEFINE_SOA(className, ...)\
		using tuple_type = std::tuple<FOR_EACH(WRAP_DECLTYPE,COMMA,__VA_ARGS__)>;\
		using soa_type = SFW::FlattenT<tuple_type>;\
		using variant_type = SFW::unique_variant_from_tuple<soa_type>;\
		static constexpr auto member_ptr_tuple = std::make_tuple(WRAP_MEMBER_FOREACH(className,WRAP_MEMBER,__VA_ARGS__));\
		struct ToPtr{private:\
			struct ToPtrTag{}; \
			FOR_EACH(WRAP_DECLTYPE_PTR,SEMICOLON,__VA_ARGS__);\
			static constexpr auto ptr_tuple = std::make_tuple(WRAP_MEMBER_FOREACH(ToPtr,WRAP_MEMBER_PTR,__VA_ARGS__)); public:\
			FOR_EACH(DEFINE_GET_FUNCTION,SPACE,__VA_ARGS__)\
			template<typename... AccessTypes> friend class SFW::ECS::ComponentAccessor;};
	}
}