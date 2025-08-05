/*****************************************************************//**
 * @file   ServiceContext.h
 * @brief サービスコンテキストを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <typeindex>

namespace SectorFW {
	namespace ECS {
		/**
		 * @brief サービスの静的な型を定義するテンプレート
		 * @detail サービスの型に対して、静的なisStaticメンバーを定義します。
		 * @tparam Services... サービスの型リスト
		 */
		template<typename... Services>
		constexpr bool AllStaticServices = (Services::isStatic && ...);
		/**
		 * @brief サービスの型にisStaticメンバーが存在するかをチェックするコンセプト
		 */
		template<typename T>
		concept HasServiceTag = requires { T::isStatic; };

		/**
		 * @brief サービスの型を定義するマクロ
		 * @detail サービスの型に対して、静的なisStaticメンバーを定義します。
		 */
#define STATIC_SERVICE_TAG static constexpr bool isStatic = true;
		 /**
		  * @brief 動的サービスの型を定義するマクロ
		  * @detail 動的サービスの型に対して、静的なisStaticメンバーを定義します。
		  */
#define DYNAMIC_SERVICE_TAG static constexpr bool isStatic = false;

		  /**
		   * @brief サービスコンテキストを定義するテンプレート
		   * @detail サービスの型をタプルとして保持します。
		   * @tparam Services... サービスの型リスト
		   */
		template<typename... Services>
		struct ServiceContext {
			static_assert((HasServiceTag<Services> && ...),
				"ServiceContext error: One or more types lack static constexpr 'isStatic'");

			using Tuple = std::tuple<Services*...>;
		};

		class IUpdateService {
			virtual void Update(double deltaTime) = 0;

		private:
			std::type_index typeIndex = typeid(IUpdateService);

			friend class ServiceLocator;
		};

		template<typename T>
		static constexpr bool isUpdateService = std::is_base_of_v<IUpdateService, T>;
	}
}
