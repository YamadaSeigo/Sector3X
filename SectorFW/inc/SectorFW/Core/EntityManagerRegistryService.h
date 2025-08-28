// EntityManagerRegistry.h
#pragma once
#include <unordered_map>
#include <shared_mutex>
#include "RegistryTypes.h"
#include "ECS/EntityManager.h"
#include "ECS/ServiceContext.hpp"

namespace SectorFW {
	// レベル/区画ごとの EntityManager を登録・解決する独立サービス
	class EntityManagerRegistry {
		struct ManagerKeyHash {
			size_t operator()(const EntityManagerKey& k) const noexcept {
				size_t h = std::hash<LevelID>{}(k.level);
				h ^= (static_cast<uint8_t>(k.scheme) * 0x9e37'79b9'7f4a'7c15ull) ^ (h << 6) ^ (h >> 2);
				h ^= (k.depth * 0x27d4'eb2d) ^ (h << 6) ^ (h >> 2);
				h ^= (k.generation * 0x1656'7b1d) ^ (h << 6) ^ (h >> 2);
				h ^= std::hash<uint64_t>{}(k.code) + 0x9e37'79b9'7f4a'7c15ull + (h << 6) + (h >> 2);
				return h;
			}
		};

	public:
		// 登録（レベル/区画ロード時）
		void RegisterOwner(const EntityManagerKey& key, ECS::EntityManager* em) {
			std::unique_lock lk(mu_);
			owners_[key] = em;
		}
		// 解除（レベル/区画アンロード時）
		void UnregisterOwner(const EntityManagerKey& key) {
			std::unique_lock lk(mu_);
			owners_.erase(key);
		}
		// 解決（生存していれば EM* を返す）
		ECS::EntityManager* ResolveOwner(const EntityManagerKey& key) const noexcept {
			std::shared_lock lk(mu_);
			auto it = owners_.find(key);
			return it == owners_.end() ? nullptr : it->second;
		}

	private:
		mutable std::shared_mutex mu_;
		std::unordered_map<EntityManagerKey, ECS::EntityManager*, ManagerKeyHash> owners_;

	public:
		STATIC_SERVICE_TAG
	};
} // namespace
