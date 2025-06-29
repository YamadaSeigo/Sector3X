/*****************************************************************//**
 * @file   AssetStorage.h
 * @brief アセットストレージを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <memory>
#include <unordered_map>

namespace SectorFW
{
	/**
	 * @brief アセットストレージのインターフェース
	 * @detail アセットの追加と取得を行うためのインターフェース
	 */
    class IAssetStorage {
    public:
        virtual ~IAssetStorage() = default;
    };
    /**
	 * @brief アセットストレージのテンプレートクラス
     */
    template<typename T>
    class AssetStorage : public IAssetStorage {
    public:
        /**
		 * @brief アセットを追加する関数
		 * @param name アセットの名前
		 * @param asset アセットのスマートポインタ
         */
        void Add(const std::string& name, std::shared_ptr<T> asset) {
            assets_[name] = std::move(asset);
        }
        /**
		 * @brief アセットを取得する関数
		 * @param name アセットの名前
		 * @return std::shared_ptr<T> アセットのスマートポインタ
         */
        std::shared_ptr<T> Get(const std::string& name) const noexcept {
            auto it = assets_.find(name);
            return it != assets_.end() ? it->second : nullptr;
        }
    private:
        /**
		 * @brief アセットを格納するマップ
         */
        std::unordered_map<std::string, std::shared_ptr<T>> assets_;
    };
}
