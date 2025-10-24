/*****************************************************************//**
 * @file   SparseComponentStore.h
 * @brief まばらなコンポーネントを管理するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "Entity.h"

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief まばらなコンポーネントを管理するクラス
		 * @tparam T コンポーネントの型
		 * @detail EntityIDをキーにして、まばらなコンポーネントを管理します。
		 */
		template<typename T>
		class SparseComponentStore {
		public:
			/**
			 * @brief エンティティのIDをキーにして、まばらなコンポーネントを追加
			 * @param id エンティティのID
			 * @param value コンポーネントの値
			 */
			void Add(EntityID id, const T& value) { components[id] = value; }
			/**
			 * @brief エンティティのIDをキーにして、まばらなコンポーネントが存在するか確認
			 * @param id エンティティのID
			 * return 存在する場合はtrue、存在しない場合はfalse
			 */
			bool Has(EntityID id) const noexcept { return components.find(id) != components.end(); }
			/**
			 * @brief エンティティのIDをキーにして、まばらなコンポーネントを取得
			 * @param id エンティティのID
			 * @return T* コンポーネントのポインタ
			 */
			T* Get(EntityID id) const noexcept {
				auto it = components.find(id);
				return it != components.end() ? &it->second : nullptr;
			}
			/**
			 * @brief エンティティのIDをキーにして、まばらなコンポーネントを削除
			 * @param id エンティティのID
			 */
			void Remove(EntityID id) { components.erase(id); }
			/**
			 * @brief まばらなコンポーネントの全てを取得
			 * @return std::unordered_map<EntityID, T>&
			 */
			std::unordered_map<EntityID, T>& GetComponents() noexcept { return components; }
		private:
			/**
			 * @brief コンポーネントを格納するマップ
			 */
			std::unordered_map<EntityID, T> components;
		};
	}
}