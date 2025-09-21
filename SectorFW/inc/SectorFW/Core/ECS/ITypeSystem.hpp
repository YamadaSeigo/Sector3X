/*****************************************************************//**
 * @file   ITypeSystem.h
 * @brief ECSシステムのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <limits>
#include <type_traits>

#include "ISystem.hpp"
#include "ComponentTypeRegistry.h"
#include "Query.h"
#include "../../Math/Frustum.hpp"
#include "../../Util/UndeletablePtr.hpp"
#include "../../Util/function_trait.h"

namespace SectorFW
{
	namespace ECS
	{
		//前方宣言
		template<typename Derived, typename Partition, typename AccessSpec, typename ContextSpec>
		class ITypeSystem;
		/**
		 * @brief StartImplのオーバーロードをチェックするコンセプト
		 */
		template<typename Derived, typename... Services>
		concept HasStartImpl =
			requires (Derived & t, UndeletablePtr<Services>... services) {
				{ t.StartImpl(services...) } -> std::same_as<void>;
		};
		// UpdateImplのオーバーロードをチェックするコンセプト
		template<typename Derived, typename Partition, typename... Services>
		concept HasUpdateImpl =
			requires (Derived & t, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(partition, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, LevelContext ctx, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(ctx, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, LevelContext & ctx, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(partition, ctx, services...) } -> std::same_as<void>;
		};

		/**
		 * @brief ECSシステムのインターフェース
		 * @tparam Derived CRTPで継承する派生クラス
		 * @tparam Partition パーティションの型
		 * @tparam AccessTypes アクセスするコンポーネントの型
		 * @tparam Services サービスの型
		 */
		template<typename Derived, typename Partition, typename... AccessTypes, typename... Services>
		class ITypeSystem<Derived, Partition, ComponentAccess<AccessTypes...>, ServiceContext<Services...>> : public ISystem<Partition> {
		protected:
			using AccessorTuple = ComponentAccess<AccessTypes...>::Tuple;
			using ContextTuple = ServiceContext<Services...>::Tuple;

			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 */
			template<typename F, typename... Args>
			void ForEachChunkWithAccessor(F&& func, Partition& partition, Args... args)
			{
				Query query;
				query.With<typename AccessPolicy<AccessTypes>::ComponentType...>();
				std::vector<ArchetypeChunk*> chunks = query.MatchingChunks<Partition&>(partition);
#ifdef _DEBUG
				if (chunks.empty() && matchingChunk) {
					matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + this->derived_name();
					LOG_WARNING(log.c_str());
				}
				else {
					matchingChunk = true;
				}
#endif
				for (auto& chunk : chunks)
				{
					ComponentAccessor<AccessTypes...> accessor(chunk);
					std::forward<F>(func)(accessor, chunk->GetEntityCount(), std::forward<Args>(args)...);
				}
			}
			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(フラスタムカリング付き)
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 * @param fru フラスタム
			 * @param ...args 追加の引数
			 */
			template<typename F, typename... Args>
			void ForEachFrustumChunkWithAccessor(F&& func, Partition& partition, Math::Frustumf& fru, Args... args)
			{
				auto cullChunks = partition.CullChunks(fru);

				if (cullChunks.empty()) return;

				Query query;
				query.With<typename AccessPolicy<AccessTypes>::ComponentType...>();
				std::vector<ArchetypeChunk*> chunks = query.MatchingChunks<decltype(cullChunks)&>(cullChunks);
#ifdef _DEBUG
				if (chunks.empty() && matchingChunk) {
					matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + this->derived_name();
					LOG_WARNING(log.c_str());
				}
				else if (!chunks.empty()) {
					matchingChunk = true;
				}
#endif
				for (auto& chunk : chunks)
				{
					ComponentAccessor<AccessTypes...> accessor(chunk);
					std::forward<F>(func)(accessor, chunk->GetEntityCount(), std::forward<Args>(args)...);
				}
			}
			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(エンティティID付き)
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 * @param ...args 追加の引数
			 */
			template<typename F, typename... Args>
			void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, Args... args)
			{
				Query query;
				query.With<typename AccessPolicy<AccessTypes>::ComponentType...>();
				std::vector<ArchetypeChunk*> chunks = query.MatchingChunks<Partition&>(partition);
#ifdef _DEBUG
				if (chunks.empty() && matchingChunk) {
					matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + this->derived_name();
					LOG_WARNING(log.c_str());
				}
				else {
					matchingChunk = true;
				}
#endif
				for (auto& chunk : chunks)
				{
					ComponentAccessor<AccessTypes...> accessor(chunk);
					std::forward<F>(func)(accessor, chunk->GetEntityCount(), chunk->GetEntities(), std::forward<Args>(args)...);
				}
			}
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
			 * @brief システムの開始関数
			 * @param partition パーティションの参照
			 * @detail 自身のコンテキストを使用して、StartImplを呼び出す
			 */
			void Start(const ServiceLocator& serviceLocator) override {
				if constexpr (HasStartImpl<Derived, Services...>) {
					if constexpr (AllStaticServices<Services...>) {
						// 静的サービスを使用する場合、サービスロケーターから直接取得
						std::apply(
							[&](Services*... unpacked) {
								static_cast<Derived*>(this)->StartImpl(UndeletablePtr<Services>(unpacked)...);
							},
							context
						);
						return;
					}

					auto serviceTuple = std::make_tuple(serviceLocator.Get<Services>()...);
					std::apply(
						[&](Services*... unpacked) {
							static_cast<Derived*>(this)->StartImpl(UndeletablePtr<Services>(unpacked)...);
						},
						serviceTuple
					);
				}
			}
			/**
			 * @brief システムの更新関数
			 * @param partition パーティションの参照
			 * @detail 自身のコンテキストを使用して、UpdateImplを呼び出す
			 */
			void Update(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator) override {
				if constexpr (HasUpdateImpl<Derived, Partition, Services...>) {
					if constexpr (AllStaticServices<Services...>) {
						// 静的サービスを使用する場合、サービスロケーターから直接取得
						std::apply(
							[&](Services*... unpacked) {
								constexpr bool hasPartition = function_mentions_v<decltype(&Derived::UpdateImpl), Partition&>;
								constexpr bool hasLevelContext = function_mentions_v<decltype(&Derived::UpdateImpl), LevelContext&>;

								if constexpr (hasPartition && hasLevelContext)
									static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (hasPartition && !hasLevelContext)
									static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (!hasPartition && hasLevelContext)
									static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
								else
									static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<Services>(unpacked)...);
							},
							context
						);
						return;
					}

					auto serviceTuple = std::make_tuple(serviceLocator.Get<Services>()...);
					std::apply(
						[&](Services*... unpacked) {
							constexpr bool hasPartition = function_mentions_v<decltype(&Derived::UpdateImpl), Partition&>;
							constexpr bool hasLevelContext = function_mentions_v<decltype(&Derived::UpdateImpl), LevelContext&>;

							if constexpr (hasPartition && hasLevelContext)
								static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
							else if constexpr (hasPartition && !hasLevelContext)
								static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
							else if constexpr (!hasPartition && hasLevelContext)
								static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
							else
								static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<Services>(unpacked)...);
						},
						serviceTuple
					);
				}
			}
			/**
			 * @brief システムのアクセス情報を取得する関数
			 * @return AccessInfo アクセス情報
			 */
			constexpr AccessInfo GetAccessInfo() const noexcept override {
				return ComponentAccess<AccessTypes...>::GetAccessInfo();
			}
		private:
			//システムのコンテキストを保持するタプル
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

#ifdef _DEBUG
			bool matchingChunk = true;
#endif //_DEBUG
		};
	}
}