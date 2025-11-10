/*****************************************************************//**
 * @file   ServiceLocator.h
 * @brief サービスロケータークラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <execution>

#include "../../Graphics/RenderService.h"
#include "../../Util/TypeChecker.hpp"
 //#include "Input/InputService.h"

// 並列サービス更新を有効にするかどうかの定義
#ifndef SFW_ENABLE_PARALLEL_SERVICE_UPDATE
//#define SFW_ENABLE_PARALLEL_SERVICE_UPDATE
#endif

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief Systemが依存するサービスを管理するクラス
		 * @detail 内部でshared_mutexを使用しているのでムーブのみ許可する
		 */
		class ServiceLocator {
			struct Location {
				void* servicePtr = nullptr;
				size_t updateIndex = 0;
				bool isStatic = false;

				Location() = default;
				explicit Location(void* ptr, size_t index = 0, bool isStaticService = false) noexcept
					: servicePtr(ptr), updateIndex(index), isStatic(isStaticService) {
				}
			};

			inline static bool created = false; // クラススコープに移動
		public:
			/**
			 * @brief コンストラクタ
			 * @detail 複数回生成すると実行時エラー
			 */
			template<PointerType... Service>
			explicit ServiceLocator(Service... service)
				: mapMutex(std::make_unique<std::shared_mutex>()) {
				assert(!created && "ServiceLocator instance already created.");
				created = true;

				(AllRegisterStaticServiceWithArg<std::remove_pointer_t<Service>>(service), ...);
			}
			/**
			 * @brief　コピーコンストラクタを削除
			 */
			ServiceLocator(const ServiceLocator&) = delete;
			/**
			 * @brief コピー代入演算子を削除
			 */
			ServiceLocator& operator=(const ServiceLocator&) = delete;
			/**
			 * @brief ムーブコンストラクタ
			 */
			ServiceLocator(ServiceLocator&&) noexcept = default;
			/**
			 * @brief ムーブ代入演算子
			 */
			ServiceLocator& operator=(ServiceLocator&&) noexcept = default;
			/**
			 * @brief デストラクタ
			 */
			~ServiceLocator() {
				created = false;
			}

			/**
			 * @brief 初期化処理(必ず呼び出す必要がある、複数回呼び出し禁止)
			 */
			template<typename... Services>
			void InitAndRegisterStaticService() noexcept {
				assert(!initialized && "ServiceLocator is already initialized.");

				std::unique_lock<std::shared_mutex> lock(*mapMutex);
				initialized = true;
				(AllRegisterStaticService<Services>(), ...);
			}
			/**
			 * @brief 動的サービスの登録を行う
			 * @tparam T サービスの型
			 */
			template<typename... T>
			void RegisterDynamicService() noexcept {
				// 動的サービスの登録を行う
				(AllRegisterDynamicService<T>(), ...);
			}
			/**
			 * @brief サービスの登録を解除する(動的なサービス限定)
			 * @tparam T サービスの型
			 */
			template<typename T>
			void UnregisterDynamicService() {
				static_assert(!T::isStatic, "Cannot unregister static service.");

				std::unique_lock<std::shared_mutex> lock(*mapMutex);
				auto iter = services.find(typeid(T));

				if (iter != services.end())
				{
					if constexpr (isUpdateService<T>) {
						auto index = iter->second.updateIndex;
						if (updateServices.empty() || index >= updateServices.size()) {
							assert(false && "Invalid update service index.");
							return;
						}

						auto updateService = updateServices[index];
						auto it = services.find(updateServices.back()->typeIndex);
						if (it != services.end()) {
							it->second.updateIndex = index;
							std::swap(updateServices[index], updateServices.back());
							updateServices.pop_back();
						}
						else {
							assert(false && "Update service not found in the update services list.");
						}
					}
					services.erase(iter);
				}
			}
			/**
			 * @brief サービスを取得する
			 * @tparam T サービスの型
			 * @return サービスのポインタ
			 */
			template<typename T>
			T* Get() const noexcept {
				//static_assert(T::isStatic || true, "Dynamic services can be null");

				std::shared_lock<std::shared_mutex> lock(*mapMutex);
				auto it = services.find(typeid(T));
				if (it == services.end()) [[unlikely]] {
					assert(!T::isStatic && "Static service not registered!");
					return nullptr;
				}
				return static_cast<T*>(it->second.servicePtr);
			}
			/**
			 * @brief サービスコンテキストが初期化されているかを確認する
			 * @return 初期化されている場合はtrue、そうでない場合はfalse
			 */
			bool IsInitialized() const noexcept {
				return initialized;
			}
			/**
			 * @brief サービスの更新を行う
			 */
			void UpdateService(double deltaTime) {
				std::shared_lock<std::shared_mutex> lock(*mapMutex);
#if defined(SFW_ENABLE_PARALLEL_SERVICE_UPDATE)
				std::for_each(
					std::execution::par,                // 並列実行
					updateServices.begin(),
					updateServices.end(),
					[deltaTime](IUpdateService* s) noexcept {
						if (s) s->Update(deltaTime);
					}
				);
#else
				for (IUpdateService* s : updateServices) {
					if (s) s->Update(deltaTime);

				}
#endif
			}
		private:
			/**
			 * @brief サービスを登録する(静的なサービス限定、引数あり)
			 * @param service サービスのポインタ
			 */
			template<typename T>
			void AllRegisterStaticServiceWithArg(T* service) noexcept {
				static_assert(T::isStatic, "Cannot register dynamic service with argument.");
				assert((!IsRegistered<T>() && service) && "Cannot register service.");

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service);
					updateService->typeIndex = typeid(T);
					updateServices.push_back(updateService);
				}

				services[typeid(T)] = Location{ service,updateIndex, T::isStatic };
			}

			/**
			 * @brief すべてのサービスを登録する
			 * @param service サービスのポインタ
			 */
			template<typename T>
			void AllRegisterStaticService() noexcept {
				static_assert(T::isStatic, "Cannot register dynamic service.");
				assert((!IsRegistered<T>()) && "Cannot re-register static service.");

				// ServiceLocator は唯一のインスタンスとして設計されており、
				// 各サービス型ごとに1つだけ static に保持して問題ない
				static std::unique_ptr<T> service = std::make_unique<T>();

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service.get());
					updateService->typeIndex = typeid(T);
					updateServices.push_back(updateService);
				}

				services[typeid(T)] = Location{ service.get(),updateIndex, T::isStatic };
			}
			/**
			 * @brief サービスを登録する(動的なサービス限定)
			 * @tparam T サービスの型
			 */
			template<typename T>
			void AllRegisterDynamicService() noexcept {
				static_assert(!T::isStatic, "Cannot re-register static service.");

				if (IsRegistered<T>()) {
					assert(false && "Service already registered.");
					return;
				}

				// ServiceLocator は唯一のインスタンスとして設計されており、
				// 各サービス型ごとに1つだけ static に保持して問題ない
				static std::unique_ptr<T> service;
				service = std::make_unique<T>();

				std::unique_lock<std::shared_mutex> lock(*mapMutex);

				size_t updateIndex = updateServices.size();
				if constexpr (isUpdateService<T>) {
					IUpdateService* updateService = static_cast<IUpdateService*>(service.get());
					updateService->typeIndex = typeid(T);
					updateServices.push_back(updateService);
				}

				services[typeid(T)] = Location{ service.get(),updateIndex,T::isStatic };
			}

			/**
			 * @brief サービスが登録されているかどうか
			 * @return 登録されている場合true
			 */
			template<typename T>
			bool IsRegistered() const {
				return services.find(typeid(T)) != services.end();
			}
			/**
			 * @brief サービスマップ(Get関数で対象の型にcastする)
			 */
			std::unordered_map<std::type_index, Location> services;
			/**
			 * @brief 初期化されているかのフラグ
			 */
			bool initialized = false;
			//サービスマップへのアクセス同期
			std::unique_ptr<std::shared_mutex> mapMutex;
			// 更新サービスのリスト
			std::vector<IUpdateService*> updateServices;
		};
	}
}
