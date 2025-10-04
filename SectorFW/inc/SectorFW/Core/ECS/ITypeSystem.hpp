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
		// EndImplのオーバーロードをチェックするコンセプト
		template<typename Derived, typename Partition, typename... Services>
		concept HasEndImpl =
			requires (Derived & t, UndeletablePtr<Services>... services) {
				{ t.EndImpl(services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, UndeletablePtr<Services>... services) {
				{ t.EndImpl(partition, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, LevelContext ctx, UndeletablePtr<Services>... services) {
				{ t.EndImpl(ctx, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, LevelContext & ctx, UndeletablePtr<Services>... services) {
				{ t.EndImpl(partition, ctx, services...) } -> std::same_as<void>;
		};

		// ComponentAccess<Override...> が Allowed... の部分集合か？
		template<class AccessSpec, class... Allowed>
		struct access_subset_impl : std::false_type {};

		template<class... Override, class... Allowed>
		struct access_subset_impl<ComponentAccess<Override...>, Allowed...>
			: std::bool_constant<(OneOf<Override, Allowed...> && ...)> {
		};

		template<class AccessSpec, class... Allowed>
		concept AccessSpecSubsetOf = access_subset_impl<AccessSpec, Allowed...>::value;

		// （オプション）Read/Write の違いを無視して「コンポーネント型だけ」比較したい場合
		template<class A> struct access_component { using type = typename AccessPolicy<A>::ComponentType; };

		template<class T, class... Allowed>
		concept OneOfByComponent =
			(std::is_same_v<typename access_component<T>::type,
				typename access_component<Allowed>::type> || ...);

		template<class AccessSpec, class... Allowed>
		struct access_subset_by_comp_impl : std::false_type {};

		template<class... Override, class... Allowed>
		struct access_subset_by_comp_impl<ComponentAccess<Override...>, Allowed...>
			: std::bool_constant<(OneOfByComponent<Override, Allowed...> && ...)> {
		};

		template<class AccessSpec, class... Allowed>
		concept AccessSpecSubsetOfByComponent =
			access_subset_by_comp_impl<AccessSpec, Allowed...>::value;

		// 1) 正規化トレイト
		template<class T>
		struct access_spec_normalize { using type = T; };

		template<class... Ts>
		struct access_spec_normalize<ComponentAccessor<Ts...>> {
			using type = ComponentAccess<Ts...>;
		};

		template<class T>
		using access_spec_normalize_t = typename access_spec_normalize<std::remove_cvref_t<T>>::type;

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
			template<typename F, typename... CallArgs>
			void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				ForEach_impl(static_cast<ComponentAccess<AccessTypes...>*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename AccessSpec, typename F, typename... CallArgs>
				requires ::SectorFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				using AS = access_spec_normalize_t<AccessSpec>;
				ForEach_impl(static_cast<AS*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename... Override, typename F, typename... CallArgs>
				requires (sizeof...(Override) > 0) &&
			::SectorFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachChunkWithAccessor<Access>(
					std::forward<F>(func), partition, std::forward<CallArgs>(args)...);
			}

			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(フラスタムカリング付き)
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 * @param fru フラスタム
			 * @param ...args 追加の引数
			 */
			 // A) 既定版：クラス定義の AccessTypes... を使う（制約なし）
			template<typename F, typename... CallArgs>
			void ForEachFrustumChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunks(fru);
				if (cullChunks.empty()) return;

				using DefaultAccess = ComponentAccess<AccessTypes...>;
				ForEachFrustum_impl(static_cast<DefaultAccess*>(nullptr),
					this, std::forward<F>(func), cullChunks,
					std::forward<CallArgs>(args)...);
			}

			// B) 上書き版（単一 AccessSpec）…部分集合であることを要求
			template<typename AccessSpec, typename F, typename... CallArgs>
				requires ::SectorFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachFrustumChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunks(fru);
				if (cullChunks.empty()) return;

				ForEachFrustum_impl(static_cast<AccessSpec*>(nullptr),
					this, std::forward<F>(func), cullChunks,
					std::forward<CallArgs>(args)...);
			}

			// C) 上書き版（従来の書き味：<Read<...>, Read<...>>）…内部で束ねて転送
			template<typename... Override, typename F, typename... CallArgs>
				requires (sizeof...(Override) > 0) &&
			::SectorFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachFrustumChunkWithAccessor(F&& func,
					Partition& partition,
					Math::Frustumf& fru,
					CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachFrustumChunkWithAccessor<Access>(
					std::forward<F>(func), partition, fru, std::forward<CallArgs>(args)...);
			}
			/**
			* @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(フラスタムカリング近い順番付き)
			* @param func 関数オブジェクトまたはラムダ式
			* @param partition 対象のパーティション
			* @param fru フラスタム
			* @param cp カメラの位置
			* @param ...args 追加の引数
			*/
			// A) 既定版：クラス定義の AccessTypes... を使う（制約なし）
			template<typename F, typename... CallArgs>
			void ForEachFrustumNearChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				Math::Vec3f cp,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunksNear(fru, cp);
				if (cullChunks.empty()) return;

				using DefaultAccess = ComponentAccess<AccessTypes...>;
				ForEachFrustum_impl(static_cast<DefaultAccess*>(nullptr),
					this, std::forward<F>(func), cullChunks,
					std::forward<CallArgs>(args)...);
			}

			// B) 上書き版（単一 AccessSpec）…部分集合であることを要求
			template<typename AccessSpec, typename F, typename... CallArgs>
				requires ::SectorFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachFrustumNearChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				Math::Vec3f cp,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunksNear(fru, cp);
				if (cullChunks.empty()) return;

				ForEachFrustum_impl(static_cast<AccessSpec*>(nullptr),
					this, std::forward<F>(func), cullChunks,
					std::forward<CallArgs>(args)...);
			}

			// C) 上書き版（従来の書き味：<Read<...>, Read<...>>）…内部で束ねて転送
			template<typename... Override, typename F, typename... CallArgs>
				requires (sizeof...(Override) > 0) &&
			::SectorFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachFrustumNearChunkWithAccessor(F&& func,
					Partition& partition,
					Math::Frustumf& fru,
					Math::Vec3f cp,
					CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachFrustumNearChunkWithAccessor<Access>(
					std::forward<F>(func), partition, fru, cp, std::forward<CallArgs>(args)...);
			}

			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(エンティティID付き)
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 * @param ...args 追加の引数
			 */
			template<typename F, typename... CallArgs>
			void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				ForEachWithIDs_impl(static_cast<ComponentAccess<AccessTypes...>*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename AccessSpec, typename F, typename... CallArgs>
				requires ::SectorFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				using AS = access_spec_normalize_t<AccessSpec>;
				ForEachWithIDs_impl(static_cast<AS*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename... Override, typename F, typename... CallArgs>
				requires (sizeof...(Override) > 0) &&
			::SectorFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachChunkWithAccessorAndEntityIDs<Access>(
					std::forward<F>(func), partition, std::forward<CallArgs>(args)...);
			}


		public:
			/**
			 * @brief  UpdateImpl関数を保持しているか？
			 * @return 保持している場合はtrue
			 * @detail ISystemのIsUpdateable関数を隠蔽
			 */
			static constexpr bool IsUpdateable() noexcept {
				if constexpr (HasUpdateImpl<Derived, Partition, Services...>) return true;
				return false;
			}
			/**
			 * @brief  UpdateImpl関数を保持しているか？
			 * @return 保持している場合はtrue
			 * @detail ISystemのIsUpdateable関数を隠蔽
			 */
			static constexpr bool IsEndSystem() noexcept {
				if constexpr (HasEndImpl<Derived, Partition, Services...>) return true;
				return false;
			}
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
			 * @brief システムの終了関数
			 * @param partition パーティションの参照
			 * @detail 自身のコンテキストを使用して、StartImplを呼び出す
			 */
			void End(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator) override {
				if constexpr (HasEndImpl<Derived, Partition, Services...>) {
					if constexpr (AllStaticServices<Services...>) {
						// 静的サービスを使用する場合、サービスロケーターから直接取得
						std::apply(
							[&](Services*... unpacked) {
								constexpr bool hasPartition = function_mentions_v<decltype(&Derived::EndImpl), Partition&>;
								constexpr bool hasLevelContext = function_mentions_v<decltype(&Derived::EndImpl), LevelContext&>;

								if constexpr (hasPartition && hasLevelContext)
									static_cast<Derived*>(this)->EndImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (hasPartition && !hasLevelContext)
									static_cast<Derived*>(this)->EndImpl(partition, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (!hasPartition && hasLevelContext)
									static_cast<Derived*>(this)->EndImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
								else
									static_cast<Derived*>(this)->EndImpl(UndeletablePtr<Services>(unpacked)...);
							},
							context
						);
						return;
					}

					auto serviceTuple = std::make_tuple(serviceLocator.Get<Services>()...);
					std::apply(
						[&](Services*... unpacked) {
							constexpr bool hasPartition = function_mentions_v<decltype(&Derived::EndImpl), Partition&>;
							constexpr bool hasLevelContext = function_mentions_v<decltype(&Derived::EndImpl), LevelContext&>;

							if constexpr (hasPartition && hasLevelContext)
								static_cast<Derived*>(this)->EndImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
							else if constexpr (hasPartition && !hasLevelContext)
								static_cast<Derived*>(this)->EndImpl(partition, UndeletablePtr<Services>(unpacked)...);
							else if constexpr (!hasPartition && hasLevelContext)
								static_cast<Derived*>(this)->EndImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
							else
								static_cast<Derived*>(this)->EndImpl(UndeletablePtr<Services>(unpacked)...);
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
		private:
			// 未マッチ用（誤用検出）
			template<class AccessSpec, class Self, class F, class Source, class... Extra>
			static void ForEach_impl(AccessSpec*, Self*, F&&, Source&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0,
					"AccessSpec must be ComponentAccess<...> / ComponentAccessor<...>");
			}

			template<class... Ts, class Self, class F, class Source, class... Extra>
			static void ForEach_impl(ComponentAccess<Ts...>*,
				Self* self, F&& func, Source& source, Extra&&... extra)
			{
				Query q;
				q.With<typename AccessPolicy<Ts>::ComponentType...>();
				auto chunks = q.MatchingChunks<Source&>(source);

#ifdef _DEBUG
				if (chunks.empty() && self->matchingChunk) {
					self->matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + self->derived_name();
					LOG_WARNING(log.c_str());
				}
				else if (!chunks.empty()) {
					self->matchingChunk = true;
				}
#endif
				for (auto* ch : chunks) {
					ComponentAccessor<Ts...> acc(ch);
					std::forward<F>(func)(acc, ch->GetEntityCount(), std::forward<Extra>(extra)...);
				}
			}

			// 展開ヘルパ（AccessSpec = ComponentAccess<Ts...> を Ts... に割り出す）
			template<class AccessSpec, class Self, class F, class CullChunks, class... Extra>
			static void ForEachFrustum_impl(Self*, F&&, CullChunks&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0, "AccessSpec must be ComponentAccess<...>");
			}

			template<template<class...> class CA, class... Ts, class Self, class F, class CullChunks, class... Extra>
			static void ForEachFrustum_impl(CA<Ts...>*, Self* self, F&& func, CullChunks& cullChunks, Extra&&... extra)
			{
				Query query;
				query.With<typename AccessPolicy<Ts>::ComponentType...>();
				auto chunks = query.MatchingChunks<CullChunks&>(cullChunks);

#ifdef _DEBUG
				if (chunks.empty() && self->matchingChunk) {
					self->matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + self->derived_name();
					LOG_WARNING(log.c_str());
				}
				else if (!chunks.empty()) {
					self->matchingChunk = true;
				}
#endif
				for (auto* chunk : chunks)
				{
					ComponentAccessor<Ts...> accessor(chunk);
					std::forward<F>(func)(accessor, chunk->GetEntityCount(), std::forward<Extra>(extra)...);
				}
			}

			// --- EntityIDs 版 ---
			template<class AccessSpec, class Self, class F, class Source, class... Extra>
			static void ForEachWithIDs_impl(AccessSpec*, Self*, F&&, Source&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0,
					"AccessSpec must be ComponentAccess<...> / ComponentAccessor<...>");
			}

			template<class... Ts, class Self, class F, class Source, class... Extra>
			static void ForEachWithIDs_impl(ComponentAccess<Ts...>*,
				Self* self, F&& func, Source& source, Extra&&... extra)
			{
				Query q;
				q.With<typename AccessPolicy<Ts>::ComponentType...>();
				auto chunks = q.MatchingChunks<Source&>(source);

#ifdef _DEBUG
				if (chunks.empty() && self->matchingChunk) {
					self->matchingChunk = false;
					std::string log = std::string("No matching chunks : ") + self->derived_name();
					LOG_WARNING(log.c_str());
				}
				else if (!chunks.empty()) {
					self->matchingChunk = true;
				}
#endif
				for (auto* ch : chunks) {
					ComponentAccessor<Ts...> acc(ch);
					std::forward<F>(func)(acc, ch->GetEntityCount(), ch->GetEntities(),
						std::forward<Extra>(extra)...);
				}
			}
		};
	}
}