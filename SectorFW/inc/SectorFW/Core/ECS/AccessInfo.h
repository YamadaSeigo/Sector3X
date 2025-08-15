/*****************************************************************//**
 * @file   AccessInfo.h
 * @brief アクセス情報に関するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <unordered_set>

#include "component.hpp"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief アクセス情報を管理する構造体
		 */
		struct AccessInfo {
			std::unordered_set<ComponentTypeID> read;
			std::unordered_set<ComponentTypeID> write;
		};
		/**
		 * @brief コンポーネントの読み取りアクセスを表すテンプレート
		 */
		template<typename T> struct Read { using Type = T; };
		/**
		 * @brief コンポーネントの書き込みアクセスを表すテンプレート
		 */
		template<typename T> struct Write { using Type = T; };
	}
}