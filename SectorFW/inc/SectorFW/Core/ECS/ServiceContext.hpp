/*****************************************************************//**
 * @file   ServiceContext.h
 * @brief サービスコンテキストを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <typeindex>

namespace SFW {
	namespace ECS {
		/**
		 * @brief サービスの静的な型を定義するテンプレート
		 * @details サービスの型に対して、静的なisStaticメンバーを定義します。
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
		 * @details サービスの型に対して、静的なisStaticメンバーを定義します。
		 */
#define STATIC_SERVICE_TAG static constexpr bool isStatic = true;
		 /**
		  * @brief 動的サービスの型を定義するマクロ
		  * @details 動的サービスの型に対して、静的なisStaticメンバーを定義します。
		  */
#define DYNAMIC_SERVICE_TAG static constexpr bool isStatic = false;

		  /**
		   * @brief Systemに注入されるサービスのコンテキストを定義するテンプレート
		   * @details サービスの型をタプルとして保持します。
		   * @tparam Services... サービスの型リスト
		   */
		template<typename... Services>
		struct ServiceContext {
			static_assert((HasServiceTag<Services> && ...),
				"ServiceContext error: One or more types lack static constexpr 'isStatic'");

			using Tuple = std::tuple<Services*...>;
		};
		/**
		 * @brief 更新サービスのインターフェース(Systemよりも前に更新)
		 * @details 使用する場合は対象のサービスに継承させる
		 */
		class IUpdateService {
			virtual void PreUpdate(double deltaTime) = 0;

		public:
			enum Phase : uint16_t {
				EALRY = 0,
				NORMAL = 1,
				LATE = 2,
				PHASE_MAX
			};

			enum Group : uint16_t {
				GROUP_SERIAL, //メインスレッドで直列実行
				GROUP_GRAPHICS,
				GROUP_PHYSICS,
				GROUP_INPUT,
				GROUP_AUDIO,
				GROUP_AI,
				GROUP_MAX
			};

		public:
			static inline constexpr uint16_t updatePhase = EALRY;
			static inline constexpr uint16_t updateGroup = GROUP_SERIAL;
			static inline constexpr uint16_t updateOrder = 0;
		private:
			std::type_index typeIndex = typeid(IUpdateService);
			uint16_t phase = 0;
			uint16_t group = 0;
			uint16_t order = 0;

			friend class ServiceLocator;
		};

		/**
		 * @brief コミットサービスのインターフェース(Systemを更新した後に呼び出される)
		 * @details 使用する場合は対象のサービスに継承させる
		 * @details とりあえず直列で更新。重い処理を載せる予定ならIUpdateServiceのように並列にしてもいい
		 */
		class ICommitService {
		public:
			virtual void Commit(double deltaTime) = 0;
		};

		/**
		 * @brief 指定した型がIUpdateServiceを継承しているかを判定するテンプレート
		 */
		template<typename T>
		static constexpr bool isUpdateService = std::is_base_of_v<IUpdateService, T>;

		/**
		 * @brief 指定した型がICommitServiceを継承しているかを判定するテンプレート
		 */
		template<typename T>
		static constexpr bool isCommitService = std::is_base_of_v<ICommitService, T>;

#define DEFINE_UPDATESERVICE_PHASE(PhaseEnum) \
			static inline constexpr uint16_t updatePhase = PhaseEnum;

#define DEFINE_UPDATESERVICE_GROUP(GroupEnum) \
			static inline constexpr uint16_t updateGroup = GroupEnum;

#define DEFINE_UPDATESERVICE_ORDER(OrderValue) \
			static inline constexpr uint16_t updateOrder = OrderValue;

		// マクロで更新フェーズ、グループ、オーダーを定義
#define DEFINE_UPDATESERVICE(PhaseEnum, GroupEnum, OrderValue) \
			DEFINE_UPDATESERVICE_PHASE(PhaseEnum) \
			DEFINE_UPDATESERVICE_GROUP(GroupEnum) \
			DEFINE_UPDATESERVICE_ORDER(OrderValue)
	}
}