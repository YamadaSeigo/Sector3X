/*****************************************************************//**
 * @file   SpatialChunkRegistryService.h
 * @brief SpatialChunkを登録・取得するサービス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <unordered_map>
#include <shared_mutex>
#include "RegistryTypes.h"
#include "ECS/EntityManager.h"
#include "ECS/ServiceContext.hpp"

namespace SFW {
	/**
	 * @brief SpatialChunkをキーで登録・取得するサービス
	 */
	class SpatialChunkRegistry {
		struct SpatialKeyHash {
			size_t operator()(const SpatialChunkKey& k) const noexcept {
				size_t h = std::hash<LevelID>{}(k.level);
				h ^= (static_cast<uint8_t>(k.scheme) * 0x9e37'79b9'7f4a'7c15ull) ^ (h << 6) ^ (h >> 2);
				h ^= (k.depth * 0x27d4'eb2d) ^ (h << 6) ^ (h >> 2);
				h ^= (k.generation * 0x1656'7b1d) ^ (h << 6) ^ (h >> 2);
				h ^= std::hash<uint64_t>{}(k.code) + 0x9e37'79b9'7f4a'7c15ull + (h << 6) + (h >> 2);
				return h;
			}
		};

	public:
		/**
		 * @brief 登録（レベル / 区画ロード時）
		 * @param key 登録するキー
		 * @param sp 登録する SpatialChunk へのポインタ
		 */
		void RegisterOwner(const SpatialChunkKey& key, SpatialChunk* sp) {
			std::unique_lock lk(mu_);
			owners_[key] = sp;
		}
		/**
		 * @brief 解除（レベル / 区画アンロード時）
		 * @param key 解除するキー
		 */
		void UnregisterOwner(const SpatialChunkKey& key) {
			std::unique_lock lk(mu_);
			owners_.erase(key);
		}
		/**
		 * @brief 解決（生存していれば EM* を返す）
		 * @param key 解決するキー
		 * @return SpatialChunk* 解決した SpatialChunk へのポインタ（存在しない場合は nullptr）
		 */
		SpatialChunk* ResolveOwner(const SpatialChunkKey& key) const noexcept {
			std::shared_lock lk(mu_);
			auto it = owners_.find(key);
			return it == owners_.end() ? nullptr : it->second;
		}
		/**
		 * @brief 指定したキーのチャンクのEMの取得（生存していれば EM* を返す）
		 * @param key 取得するキー
		 * @return ECS::EntityManager* 解決した EntityManager へのポインタ（存在しない場合は nullptr）
		 */
		ECS::EntityManager* ResolveOwnerEM(const SpatialChunkKey& key) const noexcept {
			std::shared_lock lk(mu_);
			auto it = owners_.find(key);
			return it == owners_.end() ? nullptr : &it->second->GetEntityManager();
		}

	private:
		mutable std::shared_mutex mu_;
		std::unordered_map<SpatialChunkKey, SpatialChunk*, SpatialKeyHash> owners_;

	public:
		STATIC_SERVICE_TAG
	};
} // namespace
