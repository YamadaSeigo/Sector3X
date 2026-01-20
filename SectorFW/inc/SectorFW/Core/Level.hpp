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
	enum class ELevelState : uint32_t {
		None	= 0,
		Main	= 1u << 0,
		Sub		= 1u << 1,
		Active	= 1u << 2,
		Loading = 1u << 3,
		Loaded	= 1u << 4,
	};

	constexpr ELevelState operator|(ELevelState a, ELevelState b) {
		return static_cast<ELevelState>(
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
			);
	}

	constexpr ELevelState operator&(ELevelState a, ELevelState b) {
		return static_cast<ELevelState>(
			static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
			);
	}

	constexpr ELevelState operator~(ELevelState a) {
		// 有効ビットだけ反転したいならマスクする
		constexpr uint32_t Mask =
			static_cast<uint32_t>(ELevelState::Main) |
			static_cast<uint32_t>(ELevelState::Sub) |
			static_cast<uint32_t>(ELevelState::Active) |
			static_cast<uint32_t>(ELevelState::Loading) |
			static_cast<uint32_t>(ELevelState::Loaded);

		return static_cast<ELevelState>(~static_cast<uint32_t>(a) & Mask);
	}

	constexpr ELevelState& operator|=(ELevelState& a, ELevelState b) {
		a = a | b;
		return a;
	}

	constexpr ELevelState& operator&=(ELevelState& a, ELevelState b) {
		a = a & b;
		return a;
	}

	constexpr bool HasAny(ELevelState v) {
		return static_cast<uint32_t>(v) != 0u;
	}

	constexpr bool HasAny(ELevelState v, ELevelState bits) {
		return HasAny(v & bits);
	}

	constexpr bool HasAll(ELevelState v, ELevelState bits) {
		return (static_cast<uint32_t>(v) & static_cast<uint32_t>(bits))
			== static_cast<uint32_t>(bits);
	}


	/**
	 * @brief Systemに渡すレベルの情報や操作を提供する構造体
	 */
	template<class Partition>
	struct LevelContext {

		class IRequestCommand {
			public:
			virtual ~IRequestCommand() = default;
			virtual void Execute(Level<Partition>::Session& pLevelSession) = 0;
		};

		// 2) ディファード運用
		BudgetMover mover;

		inline LevelID GetID() const noexcept { return id; }
	private:
		LevelID id = {};

		friend class Level<Partition>;
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

	//各型のLevel間でIDを共有するためにそとに逃がす
	class LelveIDManager {
		//レベルの世代を生成するための静的なアトミック変数
		//レベルごとに一意な世代を生成するために使用されます。
		static inline std::atomic<LevelID> nextID;

		template<PartitionConcept Partition>
		friend class Level;
	};

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
		 * @brief レベルへの要求を管理するセッション構造体
		 * @details 専有ロックを取得しているので、システムの更新で生成するとデッドロックの可能性あり
		 */
		struct Session
		{
			Session(Level<Partition>& _level, std::shared_mutex& _m) : level(_level), lock(_m) {}

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
						auto chunk = level.partition.GetChunk(transform->location, level.entityManagerReg, level.levelCtx.id, EOutOfBoundsPolicy::ClampToEdge); // Transformの位置に基づいてチャンクを取得
						if (chunk)
						{
							id = (*chunk)->GetEntityManager().AddEntity<Components...>(mask, components...);
						}
					}
				}
				// Transformコンポーネントが存在しない場合、グローバルエンティティマネージャーを使用
				else
				{
					id = level.partition.GetGlobalEntityManager().AddEntity<Components...>(mask, components...);
				}

				// エンティティIDが無効な場合はエラーをログ出力
				if (!id.IsValid()) {
					LOG_ERROR("EntityID is not Valid : %d", id.index);
					return std::nullopt; // エンティティの追加に失敗した場合
				}

				return id;
			}


			/**
			 * @brief 位置を指定してエンティティを追加する関数
			 * @param location エンティティの位置
			 * @param ...components 追加するコンポーネントの可変引数
			 * @return std::optional<ECS::EntityID> エンティティID
			 * */
			template<typename... Components>
			std::optional<ECS::EntityID> AddEntityWithLocation(Math::Vec3f location, const Components&... components)
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
					LOG_WARNING("AddEntityWithLocation: Transformの位置は無視されます!");
				}

				auto chunk = level.partition.GetChunk(location, level.entityManagerReg, level.levelCtx.id, EOutOfBoundsPolicy::ClampToEdge); // Transformの位置に基づいてチャンクを取得
				if (chunk)
				{
					id = (*chunk)->GetEntityManager().AddEntity<Components...>(mask, components...);
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

				id = level.partition.GetGlobalEntityManager().AddEntity<Components...>(mask, components...);

				// エンティティIDが無効な場合はエラーをログ出力
				if (!id.IsValid()) {
					LOG_ERROR("EntityID is not Valid : %d", id.index);
					return std::nullopt; // エンティティの追加に失敗した場合
				}

				return id;
			}
		private:
			Level<Partition>& level;
			std::unique_lock<std::shared_mutex> lock;
		};
	public:
		/**
		 * @brief コンストラクタ
		 * @param name レベルの名前
		 * @param state レベルの状態
		 * @param _chunkWidth チャンクの幅
		 * @param _chunkHeight チャンクの高さ
		 * @param _chunkCellSize チャンクのセルサイズ
		 */
		explicit Level(const std::string& name, SpatialChunkRegistry& reg, ELevelState _state = ELevelState::Main,
			Math::Vec3f originWS = { 0.0f,0.0f,0.0f }, ChunkSizeType _chunkWidth = DefaultChunkWidth, ChunkSizeType _chunkHeight = DefaultChunkHeight,
			float _chunkCellSize = DefaultChunkCellSize
		) noexcept
			: name(name), state(static_cast<uint32_t>(_state)), entityManagerReg(reg),
			partition(originWS, _chunkWidth, _chunkHeight, _chunkCellSize) {

			// Active を落とす（他ビットの並行更新を壊さない）
			state.fetch_and(~static_cast<uint32_t>(ELevelState::Active),
				std::memory_order_acq_rel);

			levelCtx.id = LevelID(LelveIDManager::nextID.fetch_add(1, std::memory_order_relaxed));
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
			Level(const std::string& name, SpatialChunkRegistry& reg, ELevelState _state = ELevelState::Main,
				Args&&... args) noexcept
			: name(name), state(static_cast<uint32_t>(_state)), entityManagerReg(reg),
			partition(std::forward<Args>(args)...) {

			// Active を落とす（他ビットの並行更新を壊さない）
			state.fetch_and(~static_cast<uint32_t>(ELevelState::Active),
				std::memory_order_acq_rel);

			levelCtx.id = LevelID(LelveIDManager::nextID.fetch_add(1, std::memory_order_relaxed));
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
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVEL, /*leaf=*/false, "Level : " + name });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Id : " + std::to_string(levelCtx.id) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "EntityCount : " + std::to_string(partition.GetEntityNum()) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Partition : " + std::string(typeid(Partition).name()).substr(6) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/false, "System" });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			std::shared_lock lock(updateEntityMutex);

			if constexpr (HasPartitionUpdate<Partition>) {
				partition.Update(deltaTime);
			}

			scheduler.UpdateAll(partition, levelCtx, serviceLocator, executor);

			//各スレッドでチャンクを跨いだ移動を実行
			BudgetMover::Accessor::MoverFlush(levelCtx.mover, *serviceLocator.Get<SpatialChunkRegistry>(), 2000);
		}
		/**
		 * @brief 終了処理
		 * @details SystemSchedulerのSystemのEnd関数を呼び出す
		 */
		void Clean(const ECS::ServiceLocator& serviceLocator) {
			std::unique_lock lock(updateEntityMutex);

			scheduler.CleanSystem(partition, levelCtx, serviceLocator);

			//チャンクをクリア
			partition.CleanChunk();
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
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVEL, /*leaf=*/false, "Limited Level : " + name });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Id : " + std::to_string(levelCtx.id) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "EntityCount : " + std::to_string(partition.GetEntityNum()) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Partition : " + std::string(typeid(Partition).name()).substr(6) });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
			std::shared_lock lock(updateEntityMutex);

			// 限定的なSystemだけを実行（例：位置補間やフェードアウト処理）
			for (auto& sys : limitedSystems) {
				sys->Update(partition, levelCtx, serviceLocator, executor);
			}
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
		void ChangeState(ELevelState bits, bool set = true,
			std::memory_order mo = std::memory_order_acq_rel) {
			if (set) {
				state.fetch_or(static_cast<uint32_t>(bits), mo);
				return;
			}
			state.fetch_and(~static_cast<uint32_t>(bits), mo);
		}

		/**
		 * @brief ビットを立てるのと落とすのを同時に行う関数
		 * @param setMask 立てるビットのマスク
		 * @param clearMask 落とすビットのマスク
		 */
		void UpdateState(ELevelState setBits, ELevelState clearBits,
			std::memory_order success = std::memory_order_acq_rel,
			std::memory_order fail = std::memory_order_acquire)
		{
			uint32_t expected = state.load(std::memory_order_relaxed);
			while (true) {
				uint32_t desired = (expected | static_cast<uint32_t>(setBits)) & ~static_cast<uint32_t>(clearBits);
				if (state.compare_exchange_weak(expected, desired, success, fail)) {
					return;
				}
				// 失敗時 expected が最新値に更新されるのでループ継続
			}
		}
		/**
		 * @brief レベルの状態を取得する関数
		 * @return ELevelState レベルの状態
		 */
		ELevelState GetState(std::memory_order mo = std::memory_order_acquire) const {
			return static_cast<ELevelState>(state.load(mo));
		}

		/**
		 * @brief レベルがアクティブ状態かどうかを取得する関数
		 * @return bool アクティブ状態の場合はtrue、そうでない場合はfalse
		 */
		bool IsActive(std::memory_order mo = std::memory_order_acquire) const {
			return HasAny(static_cast<ELevelState>(state.load(mo)), ELevelState::Active);
		}
		/**
		 * @brief レベルがローディング状態かどうかを取得する関数
		 * @return bool ローディング状態の場合はtrue、そうでない場合はfalse
		 */
		bool IsLoading(std::memory_order mo = std::memory_order_acquire) const {
			return HasAny(static_cast<ELevelState>(state.load(mo)), ELevelState::Loading);
		}

		/**
		 * @brief レベルのアクティブ状態を設定する関数
		 * @param active アクティブ状態
		 */
		void SetActive(bool active,
			std::memory_order mo = std::memory_order_acq_rel) {

			if (active) {
				//念のためロード済みであることを確認
				assert(state.load(std::memory_order_acquire) & static_cast<uint32_t>(ELevelState::Loaded) && "Cannot set level active when it is not loaded.");

				state.fetch_or(static_cast<uint32_t>(ELevelState::Active), mo);
			}
			else {
				state.fetch_and(~static_cast<uint32_t>(ELevelState::Active), mo);
			}
		}

		/**
		 * @brief レベルのローディングを開始し、成功したかどうかを返す関数
		 * @return bool ローディングの開始に成功した場合はtrue、そうでない場合はfalse
		 */
		bool TryBeginLoading(
			std::memory_order success = std::memory_order_acq_rel,
			std::memory_order fail = std::memory_order_acquire
		) {
			uint32_t expected = state.load(std::memory_order_relaxed);

			for (;;) {
				// すでに Loaded / Loading なら開始できない
				if (expected & static_cast<uint32_t>(ELevelState::Loaded))  return false;
				if (expected & static_cast<uint32_t>(ELevelState::Loading)) return false;

				const uint32_t desired = expected | static_cast<uint32_t>(ELevelState::Loading);

				if (state.compare_exchange_weak(expected, desired, success, fail)) {
					return true; // Loading を立てることに成功
				}
				// compare_exchange_weak が失敗すると expected が更新されるので、そのままループ継続
			}
		}

		/**
		 * @brief レベルのクリーンを開始し、成功したかどうかを返す関数
		 * @return bool クリーンの開始に成功した場合はtrue、そうでない場合はfalse
		 */
		bool TryBeginClean(
			std::memory_order success = std::memory_order_acq_rel,
			std::memory_order fail = std::memory_order_acquire
		) {
			uint32_t expected = state.load(std::memory_order_relaxed);

			for (;;) {
				// Loadedではない / Loading なら開始できない
				if (!(expected & static_cast<uint32_t>(ELevelState::Loaded)))  return false;
				if (expected & static_cast<uint32_t>(ELevelState::Loading)) return false;

				const uint32_t desired = expected | static_cast<uint32_t>(ELevelState::Loading);

				if (state.compare_exchange_weak(expected, desired, success, fail)) {
					return true; // Loading を立てることに成功
				}
				// compare_exchange_weak が失敗すると expected が更新されるので、そのままループ継続
			}
		}

		std::optional<SpatialChunk*> GetChunk(Math::Vec3f location, EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::ClampToEdge) noexcept {
			return partition.GetChunk(location, entityManagerReg, this->levelCtx.id, policy);
		}
		/**
		 * @brief Entityを更新するためのクラス取得
		 * @return std::shared_mutex& mutex
		 */
		[[nodiscard]] Session GetSession()
		{
			return Session{*this, this->updateEntityMutex };
		}

		void ShowDebugInactiveLevelInfoUI()
		{
#ifdef _ENABLE_IMGUI
			{
				auto g = Debug::BeginTreeWrite(); // lock & back buffer
				auto& frame = g.data();

				// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVEL, /*leaf=*/false, "Level(Inactive) : " + name });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Id : " + std::to_string(levelCtx.id) });
				frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_LEVELNODE, /*leaf=*/true, "Partition : " + std::string(typeid(Partition).name()).substr(6) });
			} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif
		}

	private:
		//レベルのコンテキスト
		LevelContext<Partition> levelCtx;
		//レベルの名前
		std::string name;
		//レベルの状態(仕様上・実装上ともに最も手厚く最適化される対象のuint32_tで持つ)
		std::atomic<uint32_t> state;
		//スケジューラ
		SchedulerType scheduler;
		//限定的な場合に更新するシステムのリスト
		std::vector<std::unique_ptr<SystemType>> limitedSystems;
		//分割クラスのインスタンス
		Partition partition;
		//Entityを変更する際
		std::shared_mutex updateEntityMutex;
		//EntityManagerRegistryの参照
		SpatialChunkRegistry& entityManagerReg;
	};
}