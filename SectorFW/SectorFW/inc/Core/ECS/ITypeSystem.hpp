/*****************************************************************//**
 * @file   ITypeSystem.h
 * @brief ECSシステムのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "ISystem.hpp"
#include "ComponentTypeRegistry.h"
#include "ServiceContext.h"
#include "Query.h"
#include "Util/UndeletablePtr.hpp"

namespace SectorFW
{
	namespace ECS
	{
		//前方宣言
		template<typename Partition, typename AccessSpec, typename ContextSpec>
		class ITypeSystem;

		/**
		 * @brief ECSシステムのインターフェース
		 * @tparam Partition パーティションの型
		 * @tparam AccessTypes アクセスするコンポーネントの型
		 * @tparam Services サービスの型
		 */
		template<typename Partition, typename... AccessTypes, typename... Services>
		class ITypeSystem<Partition, ComponentAccess<AccessTypes...>, ServiceContext<Services...>> : public ISystem<Partition> {
		protected:
			using AccessorTuple = ComponentAccess<AccessTypes...>::Tuple;
			using ContextTuple = ServiceContext<Services...>::Tuple;
			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 */
			template<typename F>
			void ForEachChunkWithAccessor(F&& func, Partition& partition)
			{
				Query query;
				query.With<typename AccessPolicy<AccessTypes>::ComponentType...>();
				std::vector<ArchetypeChunk*> chunks = query.MatchingChunks<Partition&>(partition);
#ifdef _DEBUG
				if (chunks.empty()) {
					LOG_WARNING("No matching chunks found for the specified access types.");
				}
#endif
				for (auto& chunk : chunks)
				{
					ComponentAccessor<AccessTypes...> accessor(chunk);
					func(accessor, chunk->GetEntityCount());
				}
			}
			/**
			 * @brief システムの更新を実装する純粋仮想関数
			 * @param partition パーティションの参照
			 * @param ...services サービスのポインタ
			 */
			virtual void UpdateImpl(Partition& partition, UndeletablePtr<Services>... services) = 0;
		public:
			/**
			 * @brief コンテキストを設定する関数
			 * @param ctx コンテキストのタプル
			 */
			void SetContext(const ServiceLocator& serviceLocator) noexcept {
				if constexpr (AllStaticServices<Services...>) {
					context = std::make_tuple(serviceLocator.Get<Services>()...);
				}
			}

			/**
			 * @brief システムの更新関数
			 * @param partition パーティションの参照
			 * @detail 自身のコンテキストを使用して、UpdateImplを呼び出す
			 */
			void Update(Partition& partition, const ServiceLocator& serviceLocator) override {
				if constexpr (AllStaticServices<Services...>) {
					// 静的サービスを使用する場合、サービスロケーターから直接取得
					std::apply(
						[&](Services*... unpacked) {
							this->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
						},
						context
					);
					return;
				}

				auto serviceTuple = std::make_tuple(serviceLocator.Get<Services>()...);
				std::apply(
					[&](Services*... unpacked) {
						this->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
					},
					serviceTuple
				);
			}
			/**
			 * @brief システムのアクセス情報を取得する関数
			 * @return AccessInfo アクセス情報
			 */
			constexpr AccessInfo GetAccessInfo() const noexcept override {
				return ComponentAccess<AccessTypes...>::GetAccessInfo();
			}
		private:
			/**
			 * @brief システムのコンテキストを保持するタプル
			 */
			ContextTuple context;
			/**
			 * @brief コンポーネントマスクを設定するヘルパー関数
			 * @param mask コンポーネントマスクへの参照
			 */
			template<typename T>
			static void SetMask(ComponentMask& mask) {
				mask.set(ComponentTypeRegistry::GetID<T::Type>());
			}
			/**
			 * @brief タプルからコンポーネントマスクを構築するヘルパー関数
			 * @param Tuple タプルの型
			 * @return ComponentMask コンポーネントマスク
			 */
			template<typename Tuple, std::size_t... Is>
			static ComponentMask BuildMaskFromTupleImpl(std::index_sequence<Is...>) {
				ComponentMask mask;
				(SetMask<std::tuple_element_t<Is, Tuple>>(mask), ...);
				return mask;
			}
			/**
			 * @brief タプルからコンポーネントマスクを構築する関数
			 * @return ComponentMask コンポーネントマスク
			 */
			template<typename Tuple>
			static ComponentMask BuildMaskFromTuple() {
				return BuildMaskFromTupleImpl<Tuple>(
					std::make_index_sequence<std::tuple_size_v<Tuple>>{});
			}
			/**
			 * @brief 必要なコンポーネントマスクを定義する
			 */
			static inline ComponentMask required = BuildMaskFromTuple<AccessorTuple>();
		};
	}
}