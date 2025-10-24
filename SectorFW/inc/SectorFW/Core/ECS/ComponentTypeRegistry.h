/*****************************************************************//**
 * @file   ComponentTypeRegistry.h
 * @brief コンポーネントの型IDとメタ情報を管理するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <type_traits>

#include "component.hpp"
#include "../../Util/OneOrMore.hpp"

namespace SFW
{
	namespace detail {
		// 型名をエラーメッセージに表示するための未定義テンプレート
		template<class> struct print_type;
	}

	namespace ECS
	{
		/**
		 * @brief コンポーネントの型IDとメタ情報を管理するクラス
		 * @detail コンポーネントIDからサイズやアライメントなどのメタ情報を動的に取得するために定義
		 */
		struct ComponentMeta {
			struct Structure {
				Structure() = default;

				explicit Structure(size_t size, size_t align) noexcept
					: size(size), align(align) {
				}

				size_t size;
				size_t align;
			};
			OneOrMore<Structure> structure;

			bool isSparse = false;
			bool isSoA = false;
		};
		/**
		 * @brief コンポーネントの型IDとそれに対応するメタ情報を登録、管理するクラス
		 */
		class ComponentTypeRegistry {
		public:
			/**
			 * @brief コンポーネントの型IDを取得します。
			 * @tparam T コンポーネントの型
			 * @return ComponentTypeID コンポーネントの型ID
			 */
			template<typename T>
			static ComponentTypeID GetID() noexcept {
				static ComponentTypeID id = counter++;
				return id;
			}
			/**
			 * @brief コンポーネントのメタ情報を設定します。(SoAコンポーネントではない場合)
			 * @param meta_structures コンポーネントのメタ情報を格納する構造体
			 */
			template<typename T>
				requires (!requires { typename T::soa_type; })
			static void SetMetaStructure(OneOrMore<ComponentMeta::Structure>& meta_structures) {
				meta_structures.add(ComponentMeta::Structure{ sizeof(T), alignof(T) });
			}
			/**
			 * @brief SoAコンポーネントのメタ情報を設定します。
			 * @param meta_structures コンポーネントのメタ情報を格納する構造体
			 */
			template<typename T>
				requires (requires { typename T::soa_type; })
			static void SetMetaStructure(OneOrMore<ComponentMeta::Structure>& meta_structures) {
				using soa_tuple = typename T::soa_type;
				SetSoAComponentMetaTuple<soa_tuple>(meta_structures);
			}
			/**
			 * @brief Tuple を I... で展開して各要素型に委譲
			 * @param meta_structures コンポーネントのメタ情報を格納する構造体
			 */
			template<class Tuple, std::size_t... I>
			static void SetSoAComponentMetaTupleImpl(OneOrMore<ComponentMeta::Structure>& meta_structures,
				std::index_sequence<I...>) {
				// 各要素型を展開して登録（要素型ごとに再帰的にSetMetaStructureされる）
				(SetMetaStructure<std::tuple_element_t<I, Tuple>>(meta_structures), ...);
			}
			/**
			 * @brief Tuple の各要素型に対して SetSoAComponentMetaTupleImpl を呼び出す
			 * @param meta_structures コンポーネントのメタ情報を格納する構造体
			 */
			template<class Tuple>
			static void SetSoAComponentMetaTuple(OneOrMore<ComponentMeta::Structure>& meta_structures) {
				constexpr auto N = std::tuple_size_v<Tuple>;
				SetSoAComponentMetaTupleImpl<Tuple>(meta_structures, std::make_index_sequence<N>{});
			}
			/**
			 * @brief 併せて：可変長版は “メンバー列を直接渡すとき専用” として維持
			 * @param meta_structures コンポーネントのメタ情報を格納する構造体
			 */
			template<typename... Ts>
			static void SetSoAComponentMeta(OneOrMore<ComponentMeta::Structure>& meta_structures) {
				(SetMetaStructure<Ts>(meta_structures), ...);
			}
			/**
			 * @brief コンポーネントを登録します。
			 */
			template<typename T>
			static void Register() noexcept {
				if constexpr (!std::is_trivially_copyable_v<T>) {
					// ここで T の完全な型名がコンパイラのエラーに出ます
					detail::print_type<T> __please_read_type_in_error{};
					static_assert(std::is_trivially_copyable_v<T>,
						"T must be std::is_trivially_copyable");
				}

				ComponentTypeID id = GetID<T>();
				assert(id < MaxComponents && "Exceeded maximum number of components. should define 'MAX_COMPONENTS_NUM'");

				OneOrMore<ComponentMeta::Structure> meta_structures;

				if constexpr (requires { typename T::soa_type; }) {
					// SoA メンバー型の trivial を個別チェック
					using tuple = typename T::soa_type;
					constexpr auto N = std::tuple_size_v<tuple>;
					// C++17 でもOKな constexpr ループが欲しければヘルパーを用意
					(void)std::initializer_list<int>{
						([] {
							using Elem = std::tuple_element_t<0, tuple>; // ダミーでテンプレ展開例
							}(), 0)
					};
					// 展開ヘルパーを使って static_assert を仕込む
					TupleTrivialAssert<tuple>();            // 下に示す補助
					SetSoAComponentMetaTuple<tuple>(meta_structures);
				}
				else {
					SetMetaStructure<T>(meta_structures);
				}

				meta[id] = { meta_structures ,is_sparse_component_v<T> ,is_soa_component_v<T> };
			}
			/**
			 * @brief SoA タプルの全要素が trivially copyable か確認する補助
			 */
			template<class Tuple, std::size_t... I>
			static void TupleTrivialAssertImpl(std::index_sequence<I...>) {
				// 失敗時に要素型名ヒントを出したい場合
				(([] {
					using Elem = std::tuple_element_t<I, Tuple>;
					if constexpr (!std::is_trivially_copyable_v<Elem>) {
						detail::print_type<Elem> __elem_type_hint{};
						static_assert(std::is_trivially_copyable_v<Elem>,
							"SoA element must be trivially copyable");
					}
					}()), ...);
			}
			/**
			 * @brief SoA タプルの全要素が trivially copyable か確認します。
			 */
			template<class Tuple>
			static void TupleTrivialAssert() {
				constexpr auto N = std::tuple_size_v<Tuple>;
				TupleTrivialAssertImpl<Tuple>(std::make_index_sequence<N>{});
			}
			/**
			 * @brief コンポーネントがまばらなコンポーネントかどうかを判定します。
			 * @return true まばらなコンポーネントである場合
			 */
			template<typename T>
			static constexpr bool IsSparse() noexcept {
				return is_sparse_component_v<T>;
			}
			/**
			 * @brief コンポーネントのメタ情報を取得します。
			 * @param id コンポーネントの型ID
			 * @return const ComponentMeta& コンポーネントのメタ情報への参照
			 */
			static const ComponentMeta& GetMeta(ComponentTypeID id) noexcept;
		private:
			//コンポーネントの型IDをカウントする静的変数
			static inline ComponentTypeID counter = 0;
			//コンポーネントのメタ情報を格納するマップ
			static inline std::unordered_map<ComponentTypeID, ComponentMeta> meta;
		};
		/**
		 * @brief コンポーネントマスクに指定されたコンポーネントをセットします。
		 * @param mask コンポーネントマスク
		 */
		template<typename T>
		void SetMask(ComponentMask& mask) noexcept {
			if constexpr (!ComponentTypeRegistry::IsSparse<T>()) {
				mask.set(ComponentTypeRegistry::GetID<T>());
			}
		}
	}
}
