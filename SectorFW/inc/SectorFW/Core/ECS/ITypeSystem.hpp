/*****************************************************************//**
 * @file   ITypeSystem.h
 * @brief ECSシステムのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <limits>
#include <type_traits>
#include <thread>
#include <vector>
#include <exception>
#include <atomic>
#include <cmath>

#include "ISystem.hpp"
#include "ComponentTypeRegistry.h"
#include "Query.h"
#include "../../Math/Frustum.hpp"
#include "../../Util/UndeletablePtr.hpp"
#include "../../Util/function_trait.h"
#include "../ThreadPoolExecutor.h"

//ParallelをtrueにしているのにExecutorがない場合警告を出す
#define SFW_WARN_NO_EXECUTOR_PARALLEL 1


namespace SFW
{
	namespace ECS
	{
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
		} ||
			requires (Derived & t, UndeletablePtr<IThreadExecutor> exe, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(exe, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, UndeletablePtr<IThreadExecutor> exe, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(partition, exe, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, LevelContext & ctx, UndeletablePtr<IThreadExecutor> exe, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(ctx, exe, services...) } -> std::same_as<void>;
		} ||
			requires (Derived & t, Partition & partition, LevelContext & ctx, UndeletablePtr<IThreadExecutor> exe, UndeletablePtr<Services>... services) {
				{ t.UpdateImpl(partition, ctx, exe, services...) } -> std::same_as<void>;
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

		namespace
		{
			// 並列化の閾値と分割粒度（必要に応じて調整）
			static constexpr uint32_t kChunksPerTask = 16;    // 1タスクあたりのチャンク数目安

			struct IsParallel { bool v; constexpr operator bool() const noexcept { return v; } };

			// 先頭の匿名namespace内の RunIndexRange を置き換え
			template<bool kParallel, class IndexFn>
			static inline void RunIndexRange(size_t size, IndexFn&& fn, IThreadExecutor* exec = nullptr)
			{
				//念のためサイズ上限チェック
				assert(size <= (std::numeric_limits<uint32_t>::max)());

				uint32_t n = static_cast<uint32_t>(size);

				if constexpr (!kParallel) {
					for (size_t i = 0; i < n; ++i) fn(i);
				}
				else {
					//あまりのスレッド数から一スレッド当たりのタスク数を決める

					const unsigned targetTasks = (unsigned)(std::min<uint32_t>)(
						(std::max<uint32_t>)(1, (n + (kChunksPerTask - 1)) / kChunksPerTask),
						exec ? exec->Concurrency() : (std::max)(1u, std::thread::hardware_concurrency()));
					const uint32_t block = (n + targetTasks - 1) / targetTasks;

					std::exception_ptr first_ex = nullptr;
					std::mutex ex_mtx;

					if (exec) [[likely]] {
						uint32_t endTasks = targetTasks - 1;
						ThreadCountDownLatch latch(endTasks);
						for (unsigned t = 0; t < endTasks; ++t) {
							const uint32_t begin = t * block;
							if (begin >= n) { latch.CountDown(); continue; }
							const uint32_t end = (std::min)(n, begin + block);
							exec->Submit([&, begin, end] {
								try { for (uint32_t i = begin; i < end; ++i) fn(i); }
								catch (...) {
									std::lock_guard lk(ex_mtx);
									if (!first_ex) first_ex = std::current_exception();
								}
								latch.CountDown();
								});
						}

						//　このスレッドも仕事をする
						const uint32_t begin = endTasks * block;
						if (begin < n) {
							const uint32_t end = (std::min)(n, begin + block);
							try { for (uint32_t i = begin; i < end; ++i) fn(i); }
							catch (...) {
								std::lock_guard lk(ex_mtx);
								if (!first_ex) first_ex = std::current_exception();
							}
						}

						latch.Wait();
						if (first_ex) std::rethrow_exception(first_ex);
					}
					else {
						// 既存（作り捨て）フォールバック
						std::vector<std::thread> threads;
						threads.reserve(targetTasks);
						for (unsigned t = 0; t < targetTasks; ++t) {
							const uint32_t begin = t * block;
							if (begin >= n) break;
							const uint32_t end = (std::min)(n, begin + block);
							threads.emplace_back([&, begin, end] {
								try { for (uint32_t i = begin; i < end; ++i) fn(i); }
								catch (...) {
									std::lock_guard lk(ex_mtx);
									if (!first_ex) first_ex = std::current_exception();
								}
								});
						}
						for (auto& th : threads) th.join();
						if (first_ex) std::rethrow_exception(first_ex);
					}
				}
			}


			// 末尾型を取り出す
			template<class... Ts> struct last_type;
			template<class T> struct last_type<T> { using type = T; };
			template<class T, class... Ts> struct last_type<T, Ts...> : last_type<Ts...> {};
			template<class... Ts> using last_t = typename last_type<Ts...>::type;

			// 末尾が IExecutor* かを判定（空パック対応）
			template<class... Extra>
			struct tail_is_executor_impl : std::false_type {};          // 0個 → false

			template<class Last>
			struct tail_is_executor_impl<Last>
				: std::bool_constant<std::is_convertible_v<
				std::remove_reference_t<Last>, IThreadExecutor*>> {};     // 1個 → それが末尾

			template<class First, class... Rest>
			struct tail_is_executor_impl<First, Rest...>
				: tail_is_executor_impl<Rest...> {
			};                      // 再帰で末尾に到達

			template<class... Extra>
			inline constexpr bool TailIsExecutor_v = tail_is_executor_impl<Extra...>::value;

			template<bool B> struct ExecWarn { static inline void touch() {} };
			// 条件が true の場合だけ、これを参照すると “警告” になる
			template<> struct [[deprecated("Parallel=true ですが末尾に IExecutor* が渡されていません。プールを使う場合は executor を末尾に渡してください。")]]
				ExecWarn<true> {
				static inline void touch() {}
			};

			// 追加引数列 (...Extra) を (executorを除いたtuple, executor*) に分割
			template<class... Extra>
			auto SplitTailExecutor(Extra&&... extra) {
				if constexpr (sizeof...(Extra) == 0) {
					return std::pair(std::tuple<>{}, (IThreadExecutor*)nullptr);
				}
				else {
					using Last = std::remove_reference_t<last_t<Extra...>>;
					if constexpr (std::is_convertible_v<Last, IThreadExecutor*>) {
						// 末尾が IExecutor* → それは抜く
						// tuple の末尾だけ落とすためのヘルパ
						auto drop_last = []<class... Es>(std::tuple<Es...> && tp) {
							constexpr size_t N = sizeof...(Es);
							return[&]<std::size_t... I>(std::index_sequence<I...>) {
								return std::tuple<std::tuple_element_t<I, std::tuple<Es...>>...>{
									std::get<I>(std::move(tp))...};
							}(std::make_index_sequence<N - 1>{});
						};
						// まず全部をタプル化
						auto all = std::tuple<Extra...>(std::forward<Extra>(extra)...);
						// 末尾 = executor
						auto exec = static_cast<IThreadExecutor*>(std::get<sizeof...(Extra) - 1>(all));
						// 末尾を落としたタプル
						auto trimmed = drop_last(std::move(all));
						return std::pair(std::move(trimmed), exec);
					}
					else {
						// 末尾が executor でない → そのまま全渡し
						return std::pair(std::tuple<Extra...>(std::forward<Extra>(extra)...), (IThreadExecutor*)nullptr);
					}
				}
			}
		}

		//前方宣言
		template<typename Derived, typename Partition, typename AccessSpec, typename ContextSpec, IsParallel ParallelUpdate = IsParallel{false}>
		class ITypeSystem;

		/**
		 * @brief ECSシステムのインターフェース
		 * @tparam Derived CRTPで継承する派生クラス
		 * @tparam Partition パーティションの型
		 * @tparam AccessTypes アクセスするコンポーネントの型
		 * @tparam Services サービスの型
		 */
		template<typename Derived, typename Partition, typename... AccessTypes, typename... Services, IsParallel ParallelUpdate>
		class ITypeSystem<Derived, Partition, ComponentAccess<AccessTypes...>, ServiceContext<Services...>, ParallelUpdate> : public ISystem<Partition> {

		protected:
			using AccessorTuple = ComponentAccess<AccessTypes...>::Tuple;
			using ContextTuple = ServiceContext<Services...>::Tuple;

			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 */
			template<IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
			void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				ForEach_impl<Type.v>(static_cast<ComponentAccess<AccessTypes...>*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename AccessSpec, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires ::SFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				using AS = access_spec_normalize_t<AccessSpec>;
				ForEach_impl<Type.v>(static_cast<AS*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename... Override, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires (sizeof...(Override) > 0) &&
			::SFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachChunkWithAccessor(F&& func, Partition& partition, CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachChunkWithAccessor<Access, Type>(
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
			template<IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
			void ForEachFrustumChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunks(fru);
				auto& gEM = partition.GetGlobalEntityManager();

				using DefaultAccess = ComponentAccess<AccessTypes...>;
				ForEachFrustum_impl<Type.v>(static_cast<DefaultAccess*>(nullptr),
					this, std::forward<F>(func), cullChunks, gEM,
					std::forward<CallArgs>(args)...);
			}

			// B) 上書き版（単一 AccessSpec）…部分集合であることを要求
			template<typename AccessSpec, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires ::SFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachFrustumChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunks(fru);
				auto& gEM = partition.GetGlobalEntityManager();

				ForEachFrustum_impl<Type.v>(static_cast<AccessSpec*>(nullptr),
					this, std::forward<F>(func), cullChunks, gEM,
					std::forward<CallArgs>(args)...);
			}

			// C) 上書き版（従来の書き味：<Read<...>, Read<...>>）…内部で束ねて転送
			template<typename... Override, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires (sizeof...(Override) > 0) &&
			::SFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachFrustumChunkWithAccessor(F&& func,
					Partition& partition,
					Math::Frustumf& fru,
					CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachFrustumChunkWithAccessor<Access, Type>(
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
			template<IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
			void ForEachFrustumNearChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				Math::Vec3f cp,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunksNear(fru, cp);
				auto& gEM = partition.GetGlobalEntityManager();

				using DefaultAccess = ComponentAccess<AccessTypes...>;
				ForEachFrustum_impl<Type.v>(static_cast<DefaultAccess*>(nullptr),
					this, std::forward<F>(func), cullChunks, gEM,
					std::forward<CallArgs>(args)...);
			}

			// B) 上書き版（単一 AccessSpec）…部分集合であることを要求
			template<typename AccessSpec, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires ::SFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachFrustumNearChunkWithAccessor(F&& func,
				Partition& partition,
				Math::Frustumf& fru,
				Math::Vec3f cp,
				CallArgs&&... args)
			{
				auto cullChunks = partition.CullChunksNear(fru, cp);
				auto& gEM = partition.GetGlobalEntityManager();

				ForEachFrustum_impl<Type.v>(static_cast<AccessSpec*>(nullptr),
					this, std::forward<F>(func), cullChunks, gEM,
					std::forward<CallArgs>(args)...);
			}

			// C) 上書き版（従来の書き味：<Read<...>, Read<...>>）…内部で束ねて転送
			template<typename... Override, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires (sizeof...(Override) > 0) &&
			::SFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachFrustumNearChunkWithAccessor(F&& func,
					Partition& partition,
					Math::Frustumf& fru,
					Math::Vec3f cp,
					CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachFrustumNearChunkWithAccessor<Access, Type>(
					std::forward<F>(func), partition, fru, cp, std::forward<CallArgs>(args)...);
			}

			/**
			 * @brief 指定したアクセス型のコンポーネントを持つチャンクに対して、関数を適用する(エンティティID付き)
			 * @param func 関数オブジェクトまたはラムダ式
			 * @param partition 対象のパーティション
			 * @param ...args 追加の引数
			 */
			template<IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
			void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				ForEachWithIDs_impl<Type.v>(static_cast<ComponentAccess<AccessTypes...>*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename AccessSpec, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires ::SFW::ECS::AccessSpecSubsetOf<access_spec_normalize_t<AccessSpec>, AccessTypes...>
			void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				using AS = access_spec_normalize_t<AccessSpec>;
				ForEachWithIDs_impl<Type.v>(static_cast<AS*>(nullptr),
					this, std::forward<F>(func), partition,
					std::forward<CallArgs>(args)...);
			}

			template<typename... Override, IsParallel Type = IsParallel{ false }, typename F, typename... CallArgs >
				requires (sizeof...(Override) > 0) &&
			::SFW::ECS::AccessSpecSubsetOf<ComponentAccess<Override...>, AccessTypes...>
				void ForEachChunkWithAccessorAndEntityIDs(F&& func, Partition& partition, CallArgs&&... args)
			{
				using Access = ComponentAccess<Override...>;
				this->template ForEachChunkWithAccessorAndEntityIDs<Access, Type>(
					std::forward<F>(func), partition, std::forward<CallArgs>(args)...);
			}

		public:
			/**
			 * @brief  UpdateImpl関数を保持しているか？
			 * @return 保持している場合はtrue
			 * @details ISystemのIsUpdateable関数を隠蔽
			 */
			static constexpr bool IsUpdateable() noexcept {
				if constexpr (HasUpdateImpl<Derived, Partition, Services...>) return true;
				return false;
			}
			/**
			 * @brief  UpdateImpl関数を保持しているか？
			 * @return 保持している場合はtrue
			 * @details ISystemのIsUpdateable関数を隠蔽
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
			 * @details 自身のコンテキストを使用して、StartImplを呼び出す
			 */
			void Start(const ServiceLocator& serviceLocator) override final {
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
			 * @details 自身のシステムのコンテキストを使用して、UpdateImplを呼び出す
			 */
			void Update(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator, IThreadExecutor* executor) override final{
				if constexpr (HasUpdateImpl<Derived, Partition, Services...>) {
					if constexpr (AllStaticServices<Services...>) {
						// 静的サービスを使用する場合、サービスロケーターから直接取得
						std::apply(
							[&](Services*... unpacked) {
								constexpr bool hasPartition = function_mentions_v<decltype(&Derived::UpdateImpl), Partition&>;
								constexpr bool hasLevelContext = function_mentions_v<decltype(&Derived::UpdateImpl), LevelContext&>;
								constexpr bool hasExecutor = function_mentions_v<decltype(&Derived::UpdateImpl), UndeletablePtr<IThreadExecutor>>;

								if constexpr (hasPartition) {
									if constexpr (hasLevelContext && hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
									else if constexpr (hasLevelContext && !hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
									else if constexpr (!hasLevelContext && hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
									else
										static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
								}
								else
								{
									if constexpr (hasLevelContext && hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
									else if constexpr (hasLevelContext && !hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
									else if constexpr (!hasLevelContext && hasExecutor)
										static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
									else
										static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<Services>(unpacked)...);
								}
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
							constexpr bool hasExecutor = function_mentions_v<decltype(&Derived::UpdateImpl), UndeletablePtr<IThreadExecutor>>;

							if constexpr (hasPartition) {
								if constexpr (hasLevelContext && hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
								else if constexpr (hasLevelContext && !hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(partition, levelCtx, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (!hasLevelContext && hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
								else
									static_cast<Derived*>(this)->UpdateImpl(partition, UndeletablePtr<Services>(unpacked)...);
							}
							else
							{
								if constexpr (hasLevelContext && hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
								else if constexpr (hasLevelContext && !hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(levelCtx, UndeletablePtr<Services>(unpacked)...);
								else if constexpr (!hasLevelContext && hasExecutor)
									static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<IThreadExecutor>(executor), UndeletablePtr<Services>(unpacked)...);
								else
									static_cast<Derived*>(this)->UpdateImpl(UndeletablePtr<Services>(unpacked)...);
							}
						},
						serviceTuple
					);
				}
			}
			/**
			 * @brief システムの終了関数
			 * @param partition パーティションの参照
			 * @details 自身のコンテキストを使用して、StartImplを呼び出す
			 */
			void End(Partition& partition, LevelContext& levelCtx, const ServiceLocator& serviceLocator) override final {
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
			constexpr AccessInfo GetAccessInfo() const noexcept override final {
				return ComponentAccess<AccessTypes...>::GetAccessInfo();
			}
			/**
			 * @brief システムの並列更新可能かを取得する関数
			 * @return bool 並列更新可能な場合はtrue
			 */
			constexpr bool IsParallelUpdate() const noexcept override final {
				return ParallelUpdate.v;
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
				mask.set(ComponentTypeRegistry::GetID<typename T::Type>());
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
			template<bool Parallel, class AccessSpec, class Self, class F, class Source, class... Extra>
			static void ForEach_impl(AccessSpec*, Self*, F&&, Source&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0,
					"AccessSpec must be ComponentAccess<...> / ComponentAccessor<...>");
			}

			template<bool Parallel, class... Ts, class Self, class F, class Source, class... Extra>
			static void ForEach_impl(ComponentAccess<Ts...>*, Self* self, F&& func, Source& source, Extra&&... extra)
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
				auto [extraPack, exec] = SplitTailExecutor(std::forward<Extra>(extra)...);

				// コンパイル時警告：Parallel=true なのに末尾 executor が無い
#if SFW_WARN_NO_EXECUTOR_PARALLEL
				if constexpr (Parallel && !TailIsExecutor_v<Extra...>) {
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wdeprecated-declarations" // ← ここで “警告” に固定
					ExecWarn<true>::touch();   // 警告を発生させる
#pragma GCC diagnostic pop
#else
					ExecWarn<true>::touch();   // 他コンパイラは次の節かCへ
#endif
				}
#endif
				//指定したインデックスのチャンクを更新する
				auto body = [&](size_t i) {
					auto* ch = chunks[i];
					ComponentAccessor<Ts...> acc(ch);
					std::apply(
						[&](auto&... unpacked) {
							std::invoke(func, acc, ch->GetEntityCount(), unpacked...);
						}, extraPack);
					};

				RunIndexRange<Parallel>(chunks.size(), body, exec);
			}


			// 展開ヘルパ（AccessSpec = ComponentAccess<Ts...> を Ts... に割り出す）
			template<bool Parallel, class AccessSpec, class Self, class F, class CullChunks, class... Extra>
			static void ForEachFrustum_impl(Self*, F&&, CullChunks&, EntityManager&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0, "AccessSpec must be ComponentAccess<...>");
			}

			template<bool Parallel, template<class...> class CA, class... Ts, class Self, class F, class CullChunks, class... Extra>
			static void ForEachFrustum_impl(CA<Ts...>*, Self* self, F&& func, CullChunks& cullChunks, EntityManager& gEM, Extra&&... extra)
			{
				Query query;
				query.With<typename AccessPolicy<Ts>::ComponentType...>();
				auto chunks = query.MatchingChunks<CullChunks&>(cullChunks);
				auto globalChunks = query.MatchingChunks<EntityManager&>(gEM);
				chunks.insert(chunks.end(), globalChunks.begin(), globalChunks.end());

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
				auto [extraPack, exec] = SplitTailExecutor(std::forward<Extra>(extra)...);

				// コンパイル時警告：Parallel=true なのに末尾 executor が無い
#if SFW_WARN_NO_EXECUTOR_PARALLEL
				if constexpr (Parallel && !TailIsExecutor_v<Extra...>) {
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wdeprecated-declarations" // ← ここで “警告” に固定
					ExecWarn<true>::touch();   // 警告を発生させる
#pragma GCC diagnostic pop
#else
					ExecWarn<true>::touch();   // 他コンパイラは次の節かCへ
#endif
				}
#endif

				auto body = [&](size_t i) {
					auto* chunk = chunks[i];
					ComponentAccessor<Ts...> accessor(chunk);
					std::apply(
						[&](auto&... unpacked) {
							std::invoke(func, accessor, chunk->GetEntityCount(), unpacked...);
						},
						extraPack
					);
					};

				RunIndexRange<Parallel>(chunks.size(), body, exec);
			}

			// --- EntityIDs 版 ---
			template<bool Parallel, class AccessSpec, class Self, class F, class Source, class... Extra>
			static void ForEachWithIDs_impl(AccessSpec*, Self*, F&&, Source&, Extra&&...) {
				static_assert(sizeof(AccessSpec) == 0,
					"AccessSpec must be ComponentAccess<...> / ComponentAccessor<...>");
			}

			template<bool Parallel, class... Ts, class Self, class F, class Source, class... Extra>
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

				auto [extraPack, exec] = SplitTailExecutor(std::forward<Extra>(extra)...);

				// コンパイル時警告：Parallel=true なのに末尾 executor が無い
#if SFW_WARN_NO_EXECUTOR_PARALLEL
				if constexpr (Parallel && !TailIsExecutor_v<Extra...>) {
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wdeprecated-declarations" // ← ここで “警告” に固定
					ExecWarn<true>::touch();   // 警告を発生させる
#pragma GCC diagnostic pop
#else
					ExecWarn<true>::touch();   // 他コンパイラは次の節かCへ
#endif
				}
#endif

				auto body = [&](size_t i) {
					auto* ch = chunks[i];
					ComponentAccessor<Ts...> acc(ch);
					// func(acc, entityCount, entityIDs, extra...)
					std::apply(
						[&](auto&... unpacked) {
							std::invoke(func, acc, ch->GetEntityCount(), ch->GetEntities(), unpacked...);
						},
						extraPack
					);
					};

				RunIndexRange<Parallel>(chunks.size(), body, exec);
			}
		};
	}
}