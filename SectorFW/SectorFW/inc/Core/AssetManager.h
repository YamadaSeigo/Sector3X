#pragma once

#include <typeindex>

#include "AssetStorage.h"

namespace SectorFW
{
    class AssetManager {
    public:
        template<typename T>
        void RegisterAsset(const std::string& name, std::shared_ptr<T> asset) {
            GetOrCreateStorage<T>()->Add(name, std::move(asset));
        }

        template<typename T>
        std::shared_ptr<T> GetAsset(const std::string& name) const {
            auto storage = GetStorage<T>();
            return storage ? storage->Get(name) : nullptr;
        }

    private:
        std::unordered_map<std::type_index, std::unique_ptr<IAssetStorage>> storages_;

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

        template<typename T>
        AssetStorage<T>* GetStorage() const {
            auto it = storages_.find(std::type_index(typeid(T)));
            return it != storages_.end() ? static_cast<AssetStorage<T>*>(it->second.get()) : nullptr;
        }
    };

}

