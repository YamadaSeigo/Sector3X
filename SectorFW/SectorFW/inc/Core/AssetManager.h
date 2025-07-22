/*****************************************************************//**
 * @file   AssetManager.h
 * @brief アセットマネージャーを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <typeindex>

#include "AssetStorage.h"

namespace SectorFW
{
	/**
	 * @brief アセットマネージャーを表すクラス
	 * @detail アセットの登録と取得を行います。
	 */
	class AssetManager {
	public:
		/**
		 * @brief アセットの登録
		 * @param name アセットの名前
		 * @param asset アセットのポインタ
		 */
		template<typename T>
		void RegisterAsset(const std::string& name, std::shared_ptr<T> asset) {
			GetOrCreateStorage<T>()->Add(name, std::move(asset));
		}
		/**
		 * @brief アセットの取得
		 * @param name アセットの名前
		 * @return std::shared_ptr<T> アセットのポインタ
		 */
		template<typename T>
		std::shared_ptr<T> GetAsset(const std::string& name) const noexcept {
			auto storage = GetStorage<T>();
			return storage ? storage->Get(name) : nullptr;
		}
	private:
		/**
		 * @brief アセットストレージのマップ
		 */
		std::unordered_map<std::type_index, std::unique_ptr<IAssetStorage>> storages_;
		/**
		 * @brief アセットストレージを取得または作成する関数
		 * @return AssetStorage<T>* アセットストレージのポインタ
		 */
		template<typename T>
		AssetStorage<T>* GetOrCreateStorage() {
			auto index = std::type_index(typeid(T));
			auto it = storages_.find(index);
			if (it == storages_.end()) {
				auto storage = std::make_unique<AssetStorage<T>>();
				auto ptr = storage.get();
				storages_[index] = std::move(storage);
				return ptr;
			}
			return static_cast<AssetStorage<T>*>(storages_[index].get());
		}
		/**
		 * @brief アセットストレージを取得する関数
		 * @return AssetStorage<T>* アセットストレージのポインタ
		 */
		template<typename T>
		AssetStorage<T>* GetStorage() const noexcept {
			auto it = storages_.find(std::type_index(typeid(T)));
			return it != storages_.end() ? static_cast<AssetStorage<T>*>(it->second.get()) : nullptr;
		}
	};
}
