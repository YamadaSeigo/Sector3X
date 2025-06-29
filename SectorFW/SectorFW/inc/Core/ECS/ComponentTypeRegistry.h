/*****************************************************************//**
 * @file   ComponentTypeRegistry.h
 * @brief コンポーネントの型IDとメタ情報を管理するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <type_traits>

#include "component.hpp"
#include "Util/OneOrMore.hpp"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief コンポーネントの型IDとメタ情報を管理するクラス
		 * @detail コンポーネントの型IDを取得し、コンポーネントのメタ情報を設定します。
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
		 * @brief コンポーネントの型IDを管理するクラス
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
				SetSoAComponentMeta<soa_tuple>(meta_structures);
			}
			/**
			 * @brief SoAコンポーネントのメタ情報を設定します。(複数のメンバーを持つ場合)
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
				ComponentTypeID id = GetID<T>();
				OneOrMore<ComponentMeta::Structure> meta_structures;
				SetMetaStructure<T>(meta_structures);
				meta[id] = { meta_structures ,is_sparse_component_v<T> ,is_soa_component_v<T> };
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
			/**
			 * @brief コンポーネントの型IDをカウントする静的変数
			 */
			static inline ComponentTypeID counter = 0;
			/**
			 * @brief コンポーネントのメタ情報を格納するマップ
			 */
			static inline std::unordered_map<ComponentTypeID, ComponentMeta> meta;
		};
		/**
		 * @brief コンポーネントマスクに指定されたコンポーネントをセットします。
		 * @param mask コンポーネントマスク
		 */
		template<typename T>
		void SetMask(ComponentMask& mask) noexcept {
			if (!ComponentTypeRegistry::IsSparse<T>()) {
				mask.set(ComponentTypeRegistry::GetID<T>());
			}
		}
	}
}
