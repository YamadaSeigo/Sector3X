#include "Core/ECS/EntityManager.h"
#include "message.h"

#include <algorithm>

namespace SectorFW
{
	namespace ECS
	{
		void EntityManager::DestroyEntity(EntityID id)
		{
			// 事前にスワップ相手候補を把握（ロック不要：chunk 側は別同期レイヤ）
			if (true) {
				std::unique_lock<std::shared_mutex> wlock(locationsMutex);
				auto it = locations.find(id);
				if (it != locations.end()) {
					EntityLocation loc = it->second;
					ArchetypeChunk* chunk = loc.chunk;
					const size_t idx = loc.index;
					const size_t lastIndexBefore = chunk->GetEntityCount() - 1;
					// 先に Remove してから locations を更新
					chunk->RemoveEntitySwapPop(idx);
					// スワップで詰められた場合、末尾ID → idx に移動するので更新
					if (idx < lastIndexBefore) {
						EntityID swappedId = ArchetypeChunk::Accessor::GetEntities(chunk)[idx]; // Remove 後は idx に来ている
						auto its = locations.find(swappedId);
						if (its != locations.end()) {
							its->second = { chunk, idx };
						}
					}
					locations.erase(it);
				}
			}
			for (auto& [_, store] : sparseStores) {
				store->Remove(id);
			}

			entityAllocator.Destroy(id);
		}

		void EntityManager::MoveAllSparseTo(EntityManager& dst)
		{
			for (auto& [_, w] : sparseStores) w->MoveAllTo(dst);
		}

		void EntityManager::MoveSparseIDsTo(EntityManager& dst, const std::vector<EntityID>& ids)
		{
			if (ids.empty()) return;
			const EntityID* p = ids.data();
			const size_t    n = ids.size();
			for (auto& [_, w] : sparseStores) w->MoveManyTo(dst, p, n);
		}

		size_t EntityManager::MergeFromAll(EntityManager& src)
		{
			// 1) Sparse を丸ごと this へ移送
			src.MoveAllSparseTo(*this);
			// 2) 非スパースをチャンク列 memcpy で移送
			const auto ids = src.GetAllEntityIDs();
			size_t moved = 0;
			for (EntityID id : ids) {
				if (!src.TryGetLocation(id)) continue;
				if (InsertWithID_ForManagerMove(id, src, *this)) ++moved;
			}
			return moved;
		}

		std::vector<EntityID> EntityManager::GetAllEntityIDs() const
		{
			std::vector<EntityID> out;
			{
				std::shared_lock<std::shared_mutex> rlock(locationsMutex);
				out.reserve(locations.size());
				for (const auto& kv : locations) out.push_back(kv.first);
			}
			// 念のため、locations に無いものも拾う（補完）
			for (auto& [mask, arch] : archetypeManager.GetAll()) {
				for (auto& ck : arch->GetChunks()) {
					const auto entities = ArchetypeChunk::Accessor::GetEntities(ck.get());
					const size_t n = ck->GetEntityCount();
					for (size_t i = 0; i < n; ++i) {
						EntityID id = entities[i];
						if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
					}
				}
			}
			return out;
		}

		ComponentMask EntityManager::GetMask(EntityID id) const noexcept
		{
			//idのチャンクが存在する場合はそのマスクを返す
			std::shared_lock<std::shared_mutex> rlock(locationsMutex);
			auto iter = locations.find(id);
			if (iter != locations.end())
			{
				return iter->second.chunk->GetComponentMask();
			}

			//idのチャンクが存在しない場合は、全アーキタイプを検索してマスクを取得する
			ComponentMask componentMask;
			for (auto& [mask, arch] : archetypeManager.GetAll()) {
				for (auto& chunk : arch->GetChunks()) {
					auto entityCount = chunk->GetEntityCount();
					const auto& entities = ArchetypeChunk::Accessor::GetEntities(chunk.get());
					for (size_t i = 0; i < entityCount; ++i) {
						if (entities[i] == id)
							return mask; // マスクが見つかったら返す
					}
				}
			}

			return componentMask;
		}
		bool EntityManager::InsertWithID_ForManagerMove(EntityID id, EntityManager& src, EntityManager& dst)
		{
			if (&src == &dst) return false;

			// src の位置情報を得る（無ければ失敗）
			auto locOpt = src.TryGetLocation(id);
			if (!locOpt) return false;
			ArchetypeChunk* srcChunk = locOpt->chunk;
			const size_t     srcIndex = locOpt->index;
			const ComponentMask mask = srcChunk->GetComponentMask();
			// 宛先側に1行確保
			Archetype* dstArch = dst.archetypeManager.GetOrCreate(mask);
			ArchetypeChunk* dstChunk = dstArch->GetOrCreateChunk();
			const size_t dstIndex = dstChunk->AddEntity(id);
			// 非スパース列をコピー
			CopyEntityColumns(srcChunk, srcIndex, dstChunk, dstIndex);
			// src からローカル除去（スパースは後で一括移送するため触らない）
			src.EraseEntityLocalNoSparse(id);
			// 宛先の locations を登録
			{
				std::unique_lock<std::shared_mutex> wlock(dst.locationsMutex);
				dst.locations[id] = { dstChunk, dstIndex };
			}
			return true;
		}
		bool EntityManager::EraseEntityLocalNoSparse(EntityID id)
		{
			std::unique_lock<std::shared_mutex> wlock(locationsMutex);
			auto it = locations.find(id);
			if (it == locations.end()) return false;
			EntityLocation loc = it->second;
			ArchetypeChunk* chunk = loc.chunk;
			const size_t idx = loc.index;
			const size_t lastIndexBefore = chunk->GetEntityCount() - 1;

			chunk->RemoveEntitySwapPop(idx);
			if (idx < lastIndexBefore) {
				EntityID swappedId = ArchetypeChunk::Accessor::GetEntities(chunk)[idx];
				auto its = locations.find(swappedId);
				if (its != locations.end()) { its->second = { chunk, idx }; }
			}
			locations.erase(it);
			return true; // ※ ID は破棄しない（世代を進めない）
		}
		void EntityManager::CopyEntityColumns(ArchetypeChunk* srcChunk, size_t srcIndex, ArchetypeChunk* dstChunk, size_t dstIndex)
		{
			if (srcChunk->GetComponentMask() != dstChunk->GetComponentMask()) {
				LOG_ERROR("Source and destination chunks have different component masks.");
				return;
			}

			//※高速化のためLayoutが同じである前提でsrcのLayoutをdstの方でも使用する
			const auto& srcLayout = ArchetypeChunk::Accessor::GetLayoutInfo(srcChunk);
			for (const auto& infos : srcLayout) {
				size_t k = 0;
				for (const auto& col : infos) {
					auto srcBase = ArchetypeChunk::Accessor::GetBuffer(srcChunk) + col.offset;
					auto dstBase = ArchetypeChunk::Accessor::GetBuffer(dstChunk) + col.offset;
					const auto* srcElem = static_cast<const uint8_t*>(srcBase) + srcIndex * col.stride;
					uint8_t* dstElem = static_cast<uint8_t*>(dstBase) + dstIndex * col.stride;
					std::memcpy(dstElem, srcElem, col.stride);
					++k;
				}
			}
		}
	}
}