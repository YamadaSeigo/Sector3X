// BuildBodiesFromIntentsSystem.hpp
#pragma once
#include <limits>

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Physics/PhysicsService.h>
#include <SectorFW/Physics/PhysicsTypes.h>
#include <SectorFW/Physics/PhysicsLayers.h>
#include <SectorFW/Core/SpatialChunkRegistryService.h>

/**
 * @brief チャンクを跨がないBody作成コマンドを作成するユーティリティ(GlobalやStatic)
 * @param e Entity
 * @param tf Transform
 * @param body CPhyBody
 * @param shape ShapeHandle
 * @return CreateBodyCmd
 */
Physics::CreateBodyCmd MakeNoMoveChunkCreateBodyCmd(
	SFW::ECS::EntityID e,
	const CTransform& tf,
	const Physics::CPhyBody& body,
	SFW::Physics::ShapeHandle shape
)
{
	using namespace SFW::Physics;

	// 現在姿勢（PhysicsInterpolation の curr）
	Mat34f tm{};
	tm.pos = tf.location;
	tm.rot = tf.rotation;

	const uint16_t layer = body.layer != CPhyBody::invalidLayer ? body.layer : (body.type == BodyType::Static ? Layers::NON_MOVING_RAY_HIT : Layers::MOVING);
	const bool kinematic = body.kinematic;

	CreateBodyCmd cmd{};
	cmd.e = e;
	cmd.owner.code = SpatialChunkKey::kInvalidCode;   // グローバル Entity 用に無効キーをセット
	cmd.shape = shape;
	cmd.worldTM = tm;
	cmd.layer = layer;
	cmd.broadphase = 0;
	cmd.kinematic = kinematic;

	return cmd;
}

/**
* @brief 生成インテント（Entity  所有 EntityManager）だけを処理して
*        CreateBodyCmd を一括で発行する。全チャンク走査はしない。
*
* 使い方:
*  - Entity 生成直後に PhysicsService::EnqueueCreateIntent(e, &em) を呼ぶ
*  - 本システムが ConsumeCreateIntents で列挙を受け取り、必要列だけ参照して作成
*/
template<class Partition>
class BuildBodiesFromIntentsSystem final : public ECS::ISystem<Partition> {
public:
	using ShapeResolverFn = std::function<Physics::ShapeHandle(ECS::EntityID)>;

	explicit BuildBodiesFromIntentsSystem(ShapeResolverFn resolver = {})
		: resolveShape(std::move(resolver)) {
	}

	void Update(Partition& partition, SFW::LevelContext<Partition>& levelCtx, const ECS::ServiceLocator& services, IThreadExecutor* executor) override {
		using namespace SFW::Physics;

		std::vector<PhysicsService::CreateIntent> intents;
		ps->SwapCreateIntents(intents);
		if (intents.empty()) return;

		bool stop = false;
		size_t createCount = 0;
		for (const auto& in : intents) {
			auto chunk = reg->ResolveOwner(in.owner);
			if (chunk == nullptr) {
				LOG_WARNING("Not find SpatialChunk");
				continue;
			}

			auto& entityMgr = chunk->GetEntityManager();

			auto locOpt = entityMgr.TryGetLocation(in.e);
			if (!locOpt) {
				LOG_WARNING("Not Get ChunkLocation");
				continue; // 既に消えているなど
			}

			ECS::ArchetypeChunk* ch = locOpt->chunk;
			const size_t row = locOpt->index;
			const auto   count = ch->GetEntityCount();
			if (row >= count) continue; // 世代ズレなど

			ComponentAccessor <
				ECS::Read<CTransform>,
				ECS::Read<CPhyBody>> chAccessor(ch);

			// 必要列だけ生ポインタで取得（SoA）
			auto tfOpt = chAccessor.Get<Read<CTransform>>();
			auto bodyOpt = chAccessor.Get<Read<CPhyBody>>();
			if (!tfOpt || !bodyOpt) continue;
			auto tf = tfOpt.value();
			auto body = bodyOpt.value();

			// すでに生成済みならスキップ（センチネル: 0xFFFFFFFF）
			if (body.body()[row].GetIndexAndSequenceNumber() != (std::numeric_limits<uint32_t>::max)())
				continue;

			// 現在姿勢（PhysicsInterpolation の curr）
			Mat34f tm{};
			tm.pos = Vec3f(tf.px()[row], tf.py()[row], tf.pz()[row]);
			tm.rot = Quatf(tf.qx()[row], tf.qy()[row], tf.qz()[row], tf.qw()[row]);

			const uint16_t layer = body.layer()[row] != CPhyBody::invalidLayer ? body.layer()[row] : (body.type()[row] == BodyType::Static ? Layers::NON_MOVING_RAY_HIT : Layers::MOVING);
			const bool kinematic = body.kinematic()[row];

			CreateBodyCmd cmd{};
			cmd.e = in.e;
			cmd.owner = in.owner;   //どの EM かを伝える
			cmd.shape = in.h;
			cmd.worldTM = tm;
			cmd.layer = layer;
			cmd.broadphase = 0;
			cmd.kinematic = kinematic;

			bool ok = ps->CreateBody(cmd); // コマンドキューへ（固定Δtで一括適用）
			if (!ok) {
				stop = true;
				break;
			}

			createCount++;
		}

		if (stop)
		{
			//生成済みのintent削除
			if (createCount > 0)
				intents.erase(intents.begin(), intents.begin() + (createCount - 1));

			//未生成のintentを戻す
			ps->SwapCreateIntents(intents);
		}
	}

	void SetContext(const ServiceLocator& serviceLocator) noexcept {
		ps = serviceLocator.Get<Physics::PhysicsService>();
		if (!ps) {
			LOG_ERROR("PhysicsServiceがサービスとして登録されていません");
			return;
		}
		reg = serviceLocator.Get<SpatialChunkRegistry>();
		if (!reg) {
			LOG_ERROR("EntityManagerRegistryがサービスとして登録されていま");
			return;
		}
	}

	ECS::AccessInfo GetAccessInfo() const noexcept override {
		// 位置情報は EntityManager 内の locations を読むだけで、チャンク列は Read
		return ECS::ComponentAccess<ECS::Read<Physics::PhysicsInterpolation>, ECS::Read<Physics::CPhyBody>>::GetAccessInfo();
	}

private:
	ShapeResolverFn resolveShape;
	Physics::PhysicsService* ps = nullptr; // PhysicsService へのポインタ（サービスロケーターから取得）
	SpatialChunkRegistry* reg = nullptr; // EntityManagerRegistry へのポインタ（サービスロケーターから取得）
};