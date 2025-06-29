/*****************************************************************//**
 * @file   Query.h
 * @brief ECSのクエリを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "ArchetypeChunk.h"

namespace SectorFW
{
	namespace ECS
	{
		//前方宣言
		class ArchetypeChunk;
		/**
		 * @brief まばらなコンポーネントを識別するトレイト
		 */
		template <typename... Args>
		concept AllDense = (!is_sparse_component_v<Args> && ...);
		/**
		 * @brief 参照のみの型を識別するトレイト
		 */
		template<typename T>
		concept ReferenceOnly = std::is_reference_v<T>;
		/**
		 * @brief 常にfalseを返す定数テンプレート
		 */
		template<typename>
		inline constexpr bool always_false = false;
		/**
		 * @brief ECSのクエリを定義するクラス
		 */
		class Query
		{
		public:
			/**
			 * @brief まばらなコンポーネントを除外するためのテンプレート
			 * @return Query& クエリ自身の参照
			 */
			template<typename... Ts>
			Query& With() noexcept {
				static_assert(AllDense<Ts...>, "All types in With must be dense components (not sparse)");
				return *this;
			}
			/**
			 * @brief クエリに含めるコンポーネントを指定するテンプレート
			 * @return Query& クエリ自身の参照
			 */
			template<typename... Ts>
				requires AllDense<Ts...>
			Query& With() noexcept {
				(required.set(ComponentTypeRegistry::GetID<Ts>()), ...);
				return *this;
			}
			/**
			 * @brief クエリから除外するコンポーネントを指定するテンプレート
			 * @return Query& クエリ自身の参照
			 */
			template<typename... Ts>
				requires AllDense<Ts...>
			Query& Without() noexcept {
				(excluded.set(ComponentTypeRegistry::GetID<Ts>()), ...);
				return *this;
			}
			/**
			 * @brief クエリにマッチするアーキタイプチャンクを取得します。
			 * @param context コンテキストを指定するテンプレート
			 * @return std::vector<ArchetypeChunk*> マッチするチャンクのベクター
			 */
			template<ReferenceOnly T>
			std::vector<ArchetypeChunk*> MatchingChunks(T context) const noexcept {
				static_assert(always_false<T> && "Query::MatchingChunks must be specialized for the context type");

				return {};
			}
		private:
			/**
			 * @brief クエリに必要なコンポーネントマスク
			 */
			ComponentMask required;
			/**
			 * @brief クエリから除外するコンポーネントマスク
			 */
			ComponentMask excluded;
		};
	}
}
