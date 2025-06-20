#pragma once

#include <memory>
#include <unordered_map>

namespace SectorFW
{
    class IAssetStorage {
    public:
        virtual ~IAssetStorage() = default;
    };

    template<typename T>
    class AssetStorage : public IAssetStorage {
    public:
        void Add(const std::string& name, std::shared_ptr<T> asset) {
            assets_[name] = std::move(asset);
        }

        std::shared_ptr<T> Get(const std::string& name) const {
            auto it = assets_.find(name);
            return it != assets_.end() ? it->second : nullptr;
        }

    private:
        std::unordered_map<std::string, std::shared_ptr<T>> assets_;
    };
}
