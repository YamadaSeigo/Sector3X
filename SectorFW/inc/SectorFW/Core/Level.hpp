/*****************************************************************//**
 * @file   Level.hpp
 * @brief レベルを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "ECS/ComponentTypeRegistry.h"
#include "ECS/SystemScheduler.hpp"
#include "Math/Transform.hpp"
#include "partition.hpp"
#include "RegistryTypes.h"

#include "Util/logger.h"
#include "Util/extract_type.hpp"

namespace SectorFW
{
	/**
	 * @brief レベルの状態を定義する列挙型
	 */
	enum class ELevelState {
		Main, // フル更新対象
		Sub   // 限定的更新対象
	};
	/**
	 * @brief デフォルトのチャンクラインサイズを定義する型
	 */
	static constexpr ChunkSizeType DefaultChunkHeight = 64; // デフォルトのチャンクラインサイズ
	/**
	 * @brief デフォルトのチャンクカラムサイズを定義する型
	 */
	static constexpr ChunkSizeType DefaultChunkWidth = 64; // デフォルトのチャンクカラムサイズ

	static constexpr float DefaultChunkCellSize = 128.0f; // デフォルトのチャンクサイズ
	/**
	 * @brief レベルを定義するクラス
	 * @tparam Partition パーティションの型
	 */
	template<PartitionConcept Partition>
	class Level {
		using SchedulerType = ECS::SystemScheduler<Partition>;
		using SystemType = ECS::ISystem<Partition>;

		using TransformType = CTransform; // Transformコンポーネントの型

	public:
		/**
		 * @brief コンストラクタ
		 * @param name レベルの名前
		 * @param state レベルの状態
		 * @param _chunkWidth チャンクの幅
		 * @param _chunkHeight チャンクの高さ
		 * @param _chunkCellSize チャンクのセルサイズ
		 */
		explicit Level(const std::string& name, EntityManagerRegistry& reg, ELevelState state = ELevelState::Main,
			ChunkSizeType _chunkWidth = DefaultChunkWidth, ChunkSizeType _chunkHeight = DefaultChunkHeight,
			ChunkSizeType _chunkCellSize = DefaultChunkCellSize) noexcept
			: name(name), state(state), chunkCellSize(_chunkCellSize),
			partition(_chunkWidth, _chunkHeight, _chunkCellSize) {
			id = LevelID(nextID.fetch_add(1, std::memory_order_relaxed));
			partition.RegisterAllChunks(reg, id);
		}
		/**
		 * @brief LevelIDの取得関数
		 */
		LevelID GetID() const noexcept { return id; }
		/**
		 * @brief 更新処理
		 */
		void Update(const ECS::ServiceLocator& serviceLocator) {
			scheduler.UpdateAll(partition, serviceLocator);
		}
		/**
		 * @brief 限定的な更新処理
		 */
		void UpdateLimited(const ECS::ServiceLocator& serviceLocator) {
			// 限定的なSystemだけを実行（例：位置補間やフェードアウト処理）
			for (auto& sys : limitedSystems) {
				sys->Update(partition, serviceLocator);
			}
		}
		/**
		 * @brief システムを追加する関数
		 * @param system システムのユニークポインタ
		 * @param limited 限定的な更新対象かどうか
		 */
		void AddSystem(std::unique_ptr<SystemType> system, bool limited = false) {
			if (limited)
				limitedSystems.push_back(std::move(system));
			else
				scheduler.AddSystem(std::move(system));
		}
		/**
		 * @brief エンティティを追加する関数
		 * @param ...components 追加するコンポーネントの可変引数
		 * @return std::optional<ECS::EntityID> エンティティIDのオプション
		 */
		template<typename... Components>
		std::optional<ECS::EntityID> AddEntity(const Components&... components)
		{
			using namespace ECS;

			ComponentMask mask;
			(SetMask<Components>(mask), ...);

			// Transformコンポーネントが存在するかどうかをチェック
			ComponentTypeID typeID = ComponentTypeRegistry::GetID<TransformType>();
			bool hasTransform = mask.test(typeID);

			EntityID id = EntityID::Invalid();

			//Transformコンポーネントが存在する場合、パーティションからチャンクを取得
			if (hasTransform)
			{
				auto transform = extract_first_of_type<TransformType>(components...);
				if (transform)
				{
					auto chunk = partition.GetChunk(transform->location, EOutOfBoundsPolicy::ClampToEdge); // Transformの位置に基づいてチャンクを取得
					if (chunk)
					{
						id = (*chunk)->GetEntityManager().AddEntity<Components...>(mask, components...);
					}
				}
			}
			// Transformコンポーネントが存在しない場合、グローバルエンティティマネージャーを使用
			else
			{
				id = partition.GetGlobalEntityManager().AddEntity<Components...>(mask, components...);
			}

			// エンティティIDが無効な場合はエラーをログ出力
			if (!id.IsValid()) {
				LOG_ERROR("EntityID is not Valid : %d", id.index);
				return std::nullopt; // エンティティの追加に失敗した場合
			}

			return id;
		}
		/**
		 * @brief グローバルなエンティティマネージャーを取得する関数
		 * @return const ECS::EntityManager& グローバルエンティティマネージャーへの参照
		 */
		const ECS::EntityManager& GetGlobalEntityManager() noexcept { return partition.GetGlobalEntityManager(); }
		/**
		 * @brief スケジューラを取得する関数
		 * @return SchedulerType& スケジューラへの参照
		 */
		SchedulerType& GetScheduler() noexcept { return scheduler; }
		/**
		 * @brief レベルの名前を取得する関数
		 * @return const std::string& レベルの名前への参照
		 */
		const std::string& GetName() const noexcept { return name; }
		/**
		 * @brief レベルの状態を設定する関数
		 * @param s レベルの状態
		 */
		void SetState(ELevelState s) noexcept { state = s; }
		/**
		 * @brief レベルの状態を取得する関数
		 * @return ELevelState レベルの状態
		 */
		ELevelState GetState() const noexcept { return state; }
		
		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location, EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept {
			return partition.GetChunk(location, policy);
		}
	private:
		/**
		 * @brief レベルの世代を生成するための静的なアトミック変数
		 * @detail レベルごとに一意な世代を生成するために使用されます。
		 */
		static inline std::atomic<LevelID> nextID;
		/**
		 * @brief レベルの一意なID
		 */
		LevelID id = 0; // レベルの一意なID（必要に応じて使用）
		/**
		 * @brief レベルの名前
		 */
		std::string name;
		/**
		 * @brief レベルの状態
		 */
		ELevelState state;
		/**
		 * @brief スケジューラ
		 */
		SchedulerType scheduler;
		/**
		 * @brief 限定的なシステムのリスト
		 * @detail 限定的な更新対象のシステムを格納します。
		 */
		std::vector<std::unique_ptr<SystemType>> limitedSystems;
		/**
		 * @brief パーティション
		 */
		Partition partition;
		/**
		 * @brief チャンクのセルサイズ
		 */
		ChunkSizeType chunkCellSize;
	};
}