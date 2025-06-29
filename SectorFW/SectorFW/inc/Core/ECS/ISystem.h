/*****************************************************************//**
 * @file   ISystem.h
 * @brief システムのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "Accessor.h"
#include "Util/Grid.hpp"

namespace SectorFW
{
	namespace ECS
	{
		//前方定義
		class EntityManager;
		class SpatialChunk;

		/**
		 * @brief システムのインターフェース
		 * @detail 分割のクラスを指定する
		 * @tparam Partition パーティションの型
		 */
		template<typename Partition>
		class ISystem {
		public:
			/**
			 * @brief システムの更新関数
			 * @param partition 対象のパーティション
			 */
			virtual void Update(Partition& partition) = 0;
			/**
			 * @brief アクセス情報の取得関数
			 * @return AccessInfo アクセス情報
			 */
			virtual AccessInfo GetAccessInfo() const noexcept = 0;
			/**
			 * @brief デストラクタ
			 */
			virtual ~ISystem() = default;
		};
	}
}
