/*****************************************************************//**
 * @file   function_trait.h
 * @brief 関数の特定の型があるかどうかを調べるヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#include <tuple>
#include <type_traits>

namespace SectorFW
{
	// ---- function_traits（最小限） ----
	template<class>
	struct function_traits;

	// 関数型
	template<class R, class... A>
	struct function_traits<R(A...)> {
		using result_type = R;
		using args = std::tuple<A...>;
		static constexpr std::size_t arity = sizeof...(A);
	};

	// 関数ポインタ
	template<class R, class... A>
	struct function_traits<R(*)(A...)> : function_traits<R(A...)> {};

	// 関数参照
	template<class R, class... A>
	struct function_traits<R(&)(A...)> : function_traits<R(A...)> {};
	template<class R, class... A>
	struct function_traits<R(&&)(A...)> : function_traits<R(A...)> {};

	// メンバ関数ポインタ（cv/ref/ noexcept もざっくり対応）
	template<class C, class R, class... A>
	struct function_traits<R(C::*)(A...)> : function_traits<R(A...)> {};
	template<class C, class R, class... A>
	struct function_traits<R(C::*)(A...) const> : function_traits<R(A...)> {};
	template<class C, class R, class... A>
	struct function_traits<R(C::*)(A...)&> : function_traits<R(A...)> {};
	template<class C, class R, class... A>
	struct function_traits<R(C::*)(A...) const&> : function_traits<R(A...)> {};
	template<class C, class R, class... A>
	struct function_traits<R(C::*)(A...) noexcept> : function_traits<R(A...)> {};
	// 必要なら他の修飾も追加

	// ラムダ/関数オブジェクト：operator() から抽出
	template<class F>
	struct function_traits : function_traits<decltype(&F::operator())> {};

	// ---- U を含むか？ ----

	template<class F, class U>
	struct function_mentions
	{
	private:
		using R = typename function_traits<F>::result_type;
		using A = typename function_traits<F>::args;

		template<std::size_t... I>
		static consteval bool contains_in_args(std::index_sequence<I...>) {
			return (std::is_same_v<U, std::tuple_element_t<I, A>> || ...);
		}

	public:
		static constexpr bool value =
			std::is_same_v<U, R> ||
			contains_in_args(std::make_index_sequence<std::tuple_size_v<A>>{});
	};

	template<class F, class U>
	inline constexpr bool function_mentions_v = function_mentions<F, U>::value;
}
