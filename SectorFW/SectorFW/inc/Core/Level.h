#pragma once

#include "ECS/ComponentTypeRegistry.h"
#include "ECS/SystemScheduler.h"
#include "Util/Transform.h"
#include "Partition.h"

#include "Util/logger.hpp"
#include "Util/extract_type.h"

namespace SectorFW
{
	//----------------------------------------------
	// Level State Enum
	//----------------------------------------------
	enum class ELevelState {
		Main, // フル更新対象
		Sub   // 限定的更新対象
	};

	static constexpr ChunkSizeType DefaultChunkHeight = 64; // デフォルトのチャンクラインサイズ
	static constexpr ChunkSizeType DefaultChunkWidth = 64; // デフォルトのチャンクカラムサイズ

	static constexpr float DefaultChunkCellSize = 128.0f; // デフォルトのチャンクサイズ

	template<PartitionConcept Partition>
	class Level {
		using SchedulerType = ECS::SystemScheduler<Partition>;
		using SystemType = ECS::ISystem<Partition>;

	public:
		explicit Level(const std::string& name, ELevelState state = ELevelState::Main,
			ChunkSizeType _chunkWidth = DefaultChunkWidth, ChunkSizeType _chunkHeight = DefaultChunkHeight,
			ChunkSizeType _chunkCellSize = DefaultChunkCellSize) noexcept
			: name(name), state(state), chunkCellSize(_chunkCellSize),
			partition(_chunkWidth, _chunkHeight, _chunkCellSize){}

		void Update() {
			scheduler.UpdateAll(partition);
		}

		void UpdateLimited() {
			// 限定的なSystemだけを実行（例：位置補間やフェードアウト処理）
			for (auto& sys : limitedSystems) {
				sys->Update(partition);
			}
		}

		void AddSystem(std::unique_ptr<SystemType> system, bool limited = false) {
			if (limited)
				limitedSystems.push_back(std::move(system));
			else
				scheduler.AddSystem(std::move(system));
		}

		template<typename... Components>
		std::optional<ECS::EntityID> AddEntity(const Components&... components)
		{
			using namespace ECS;

			ComponentMask mask;
			(SetMask<Components>(mask), ...);

			// Transformコンポーネントが存在するかどうかをチェック
			ComponentTypeID typeID = ComponentTypeRegistry::GetID<Transform>();
			bool hasTransform = mask.test(typeID);

			EntityID id = EntityID::Invalid();

			if (hasTransform)
			{
				auto transform = extract_first_of_type<Transform>(components...);
				if (transform)
				{
					auto chunk = partition.GetChunk(transform->location); // Transformの位置に基づいてチャンクを取得
					if (chunk)
					{
						id = (*chunk)->GetEntityManager().AddEntity<Components...>(mask, components...);
					}
				}
			}
			else
			{
				id = entityManager.AddEntity<Components...>(mask, components...);
			}

			// エンティティIDが無効な場合はエラーをログ出力
			if (!id.IsValid()) {
				LOG_ERROR("Entity Count is Overflow : %d\n", ECS::EntityManager::GetEntityAllocator().NextIndex());
				return std::nullopt; // エンティティの追加に失敗した場合
			}

			return id;
		}

		ECS::EntityManager& GetEntityManager() noexcept { return entityManager; }
		SchedulerType& GetScheduler() noexcept { return scheduler; }
		const std::string& GetName() const noexcept { return name; }
		void SetState(ELevelState s) noexcept { state = s; }
		ELevelState GetState() const noexcept { return state; }

	private:
		std::string name;
		ELevelState state;
		ECS::EntityManager entityManager;
		SchedulerType scheduler;
		std::vector<std::unique_ptr<SystemType>> limitedSystems;
		Partition partition;
		ChunkSizeType chunkCellSize;
	};
}