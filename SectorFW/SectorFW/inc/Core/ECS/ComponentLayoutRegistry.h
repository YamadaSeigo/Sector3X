/*****************************************************************//**
 * @file   ComponentLayoutRegistry.h
 * @brief コンポーネントレイアウトレジストリを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <unordered_map>
#include <mutex>

#include "component.hpp"
#include "Util/OneOrMore.hpp"

namespace SectorFW
{
	namespace ECS
	{
		/**
		 * @brief コンポーネントレイアウトを定義する構造体
		 * @detail コンポーネントの型IDとその情報を格納します。
		 */
		struct ComponentLayout
		{
			std::unordered_map<ComponentTypeID, OneOrMore<ComponentInfo>> info;
			size_t capacity = 0; // チャンクの容量
		};
		/**
		 * @brief コンポーネントレイアウトレジストリを管理するクラス
		 */
		class ComponentLayoutRegistry
		{
		public:
			/**
			 * @brief コンポーネントマスクに対応するコンポーネントレイアウトを取得します。
			 * @param mask コンポーネントマスク
			 * @return const ComponentLayout& コンポーネントレイアウトへの参照
			 */
			static const ComponentLayout& GetLayout(const ComponentMask& mask) noexcept;
		private:
			/**
			 * @brief 新しいコンポーネントレイアウトを追加します。
			 * @param mask コンポーネントマスク
			 * @detail マスクに含まれるコンポーネントのメタ情報を基にレイアウトを計算します。
			 */
			static void AddNewComponentLayout(const ComponentMask& mask);
			/**
			 * @brief コンポーネントレイアウトを格納するマップ
			 */
			static inline std::unordered_map<ComponentMask, ComponentLayout> componentLayout;
			/**
			 * @brief コンポーネントレイアウトのマップへのアクセスを保護するミューテックス
			 */
			static inline std::mutex map_mutex;
		};
	}
}
