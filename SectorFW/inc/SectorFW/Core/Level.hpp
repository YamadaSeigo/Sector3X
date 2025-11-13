/*****************************************************************//**
 * @file   Level.hpp
 * @brief レベルを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "ECS/ComponentTypeRegistry.h"
#include "ECS/SystemScheduler.hpp"
#include "partition.hpp"
#include "RegistryTypes.h"
#include "ChunkCrossingMove.hpp"

#include "../Math/Transform.hpp"
#include "../Debug/logger.h"
#include "../Util/extract_type.hpp"

#ifdef _ENABLE_IMGUI
#include "../Debug/UIBus.h"
#endif // _ENABLE_IMGUI

namespace SFW
{
	/**
	 * @brief レベルの状態を定義する列挙型
	 */
	enum class ELevelState {
		Main, // フル更新対象
		Sub   // 限定的更新対象
	};

	/**
	 * @brief Systemに渡すレベルの情報や操作を提供する構造体
	 */
	struct LevelContext {
		// 2) ディファード運用
		BudgetMover mover;

		inline LevelID GetID() const noexcept { return id; }
	private:
		LevelID id = {};

		template<PartitionConcept Partition>
		friend class Level;
	};

	/**
	 * @brief デフォルトのチャンクラインサイズを定義する型
	 */
	static constexpr ChunkSizeType DefaultChunkHeight = 32; // デフォルトのチャンクラインサイズ
	/**
	 * @brief デフォルトのチャンクカラムサイズを定義する型
	 */
	static constexpr ChunkSizeType DefaultChunkWidth = 32; // デフォルトのチャンクカラムサイズ

	static constexpr float DefaultChunkCellSize = 128.0f; // デフォルトのチャンクサイズ
	/**
	 * @brief レベル(シーン単位)を定義するクラス
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
		explicit Level(const std::string& name, SpatialChunkRegistry& reg, ELevelState state = ELevelState::Main,
			ChunkSizeType _chunkWidth = DefaultChunkWidth, ChunkSizeType _chunkHeight = DefaultChunkHeight,
			float _chunkCellSize = DefaultChunkCellSize
		) noexcept
			: name(name), state(state), entityManagerReg(reg),
			partition(_chunkWidth, _chunkHeight, _chunkCellSize) {
			levelCtx.id = LevelID(nextID.fetch_add(1, std::memory_order_relaxed));
			partition.RegisterAllChunks(reg, levelCtx.id);
		}
		/**
		 * @brief T が Args... で構築可能なときだけ、このコンストラクタを有効化
		 * @
		 */
		template<class... Args>
			requires std::constructible_from<Partition, Args...> &&
		(!(std::same_as<std::remove_cvref_t<Args>, Level> || ...)) // 自分自身のコピー/ムーブと衝突しないようガード
			explicit(sizeof...(Args) == 1) // 引数1個のときだけ explicit、などの好み調整も可
			Level(const std::string& name, SpatialChunkRegistry& reg, ELevelState state = ELevelState::Main,
				Args&&... args) noexcept
			: name(name), state(state), entityManagerReg(reg),
			partition(std::forward<Args>(args)...) {
			levelCtx.id = LevelID(nextID.fetch_add(1, std::memory_order_relaxed));
			partition.RegisterAllChunks(reg, levelCtx.id);
		}
		/**
		 * @brief LevelIDの取得関数
		 */
		LevelID GetID() const noexcept { return levelCtx.id; }
		/**
		 * @brief 更新処理
		 */
		void Update(const ECS::ServiceLocator& serviceLocator, double deltaTime, IThreadExecutor* executor) {
#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();

				// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::Level, /*leaf=*/false, "Level : " + std::to_string(levelCtx.id) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::LevelNode, /*leaf=*/true, "EntityCount : " + std::to_string(partition.GetEntityNum()) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::LevelNode, /*leaf=*/true, "Partition : " + std::string(typeid(Partition).name()).substr(6) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::LevelNode, /*leaf=*/false, "System" });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif

			if constexpr (HasPartitionUpdate<Partition>) {
				partition.Update(deltaTime);
			}

			scheduler.UpdateAll(partition, levelCtx, serviceLocator, executor);

			//書くスレッドでチャンクを跨いだ移動を実行
			BudgetMover::Accessor::MoverFlush(levelCtx.mover, *serviceLocator.Get<SpatialChunkRegistry>(), 2000);
		}
		/**
		 * @brief 終了処理
		 * @detail SystemSchedulerのSystemのEnd関数を呼び出す
		 */
		void Clean(const ECS::ServiceLocator& serviceLocator) {
			scheduler.CleanSystem(partition, levelCtx, serviceLocator);
		}

		/**
		 * @brief 限定的な更新処理
		 */
		void UpdateLimited(const ECS::ServiceLocator& serviceLocator, double deltaTime, IThreadExecutor* executor) {
#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();

				// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::Level, /*leaf=*/true, "Limited Level : " + std::to_string(levelCtx.id) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::LevelNode, /*leaf=*/true, "EntityCount : " + std::to_string(partition.GetEntityNum()) });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			// 限定的なSystemだけを実行（例：位置補間やフェードアウト処理）
			for (auto& sys : limitedSystems) {
				sys->Update(partition, levelCtx, serviceLocator, executor);
			}
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
					auto chunk = partition.GetChunk(transform->location, entityManagerReg, this->levelCtx.id, EOutOfBoundsPolicy::ClampToEdge); // Transformの位置に基づいてチャンクを取得
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
		 * @brief グローバルエンティティを追加する関数
		 * @param ...components 追加するコンポーネントの可変引数
		 * @return std::optional<ECS::EntityID> エンティティIDのオプション
		 */
		template<typename... Components>
		std::optional<ECS::EntityID> AddGlobalEntity(const Components&... components)
		{
			using namespace ECS;

			ComponentMask mask;
			(SetMask<Components>(mask), ...);

			EntityID id = EntityID::Invalid();

			id = partition.GetGlobalEntityManager().AddEntity<Components...>(mask, components...);

			// エンティティIDが無効な場合はエラーをログ出力
			if (!id.IsValid()) {
				LOG_ERROR("EntityID is not Valid : %d", id.index);
				return std::nullopt; // エンティティの追加に失敗した場合
			}

			return id;
		}

		/**
		 * @brief すべてのチャンクのEntityManagerをレジスターに登録する
		 * @param reg EntityManagerRegistryの参照
		 */
		void RegisterAllChunks(SpatialChunkRegistry& reg) {
			partition.RegisterAllChunks(reg, levelCtx.id);
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
			return partition.GetChunk(location, entityManagerReg, this->levelCtx.id, policy);
		}
	private:
		//レベルの世代を生成するための静的なアトミック変数
		//レベルごとに一意な世代を生成するために使用されます。
		static inline std::atomic<LevelID> nextID;
		//レベルのコンテキスト
		LevelContext levelCtx;
		//レベルの名前
		std::string name;
		//レベルの状態
		ELevelState state;
		//スケジューラ
		SchedulerType scheduler;
		//限定的なシステムのリスト
		//限定的な更新対象のシステムを格納します。
		std::vector<std::unique_ptr<SystemType>> limitedSystems;
		//パーティション
		Partition partition;
		//EntityManagerRegistryの参照
		SpatialChunkRegistry& entityManagerReg;
	};
}