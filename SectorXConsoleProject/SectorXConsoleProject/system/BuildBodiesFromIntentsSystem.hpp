// BuildBodiesFromIntentsSystem.hpp
#pragma once
#include <limits>

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Physics/PhysicsService.h>
#include <SectorFW/Physics/PhysicsTypes.h>
#include <SectorFW/Physics/PhysicsLayers.h>
#include <SectorFW/Core/EntityManagerRegistryService.h>

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

	void Update(Partition& partition, const ECS::ServiceLocator& services) override {
		using namespace SectorFW::Physics;

		std::vector<PhysicsService::CreateIntent> intents;
		ps->ConsumeCreateIntents(intents);
		if (intents.empty()) return;

		for (const auto& in : intents) {
			ECS::EntityManager* owner = reg->ResolveOwner(in.owner);
			if (owner == nullptr) continue;

			auto locOpt = owner->TryGetLocation(in.e);
			if (!locOpt) continue; // 既に消えているなど

			ECS::ArchetypeChunk* ch = locOpt->chunk;
			const size_t row = locOpt->index;
			const auto   count = ch->GetEntityCount();
			if (row >= count) continue; // 世代ズレなど

			ComponentAccessor <
				ECS::Read<PhysicsInterpolation>,
				ECS::Read<BodyComponent>> chAccessor(ch);

			// 必要列だけ生ポインタで取得（SoA）
			auto interpOpt = chAccessor.Get<Read<PhysicsInterpolation>>();
			auto bodyOpt = chAccessor.Get<Read<BodyComponent>>();
			if (!interpOpt || !bodyOpt) continue;
			auto interp = interpOpt.value();
			auto body = bodyOpt.value();

			// すでに生成済みならスキップ（センチネル: 0xFFFFFFFF）
			if (body.body()[row].GetIndexAndSequenceNumber() != (std::numeric_limits<uint32_t>::max)())
				continue;

			// 現在姿勢（PhysicsInterpolation の curr）
			Mat34f tm{};
			tm.pos = Vec3f(interp.cpx()[row], interp.cpy()[row], interp.cpz()[row]);
			tm.rot = Quatf(interp.crx()[row], interp.cry()[row], interp.crz()[row], interp.crw()[row]);

			const uint16_t layer = body.isStatic()[row] ? Layers::NON_MOVING : Layers::MOVING;
			const bool kinematic = !!body.kinematic()[row];

			CreateBodyCmd cmd{};
			cmd.e = in.e;
			cmd.owner = in.owner;   //どの EM かを伝える
			cmd.shape = in.h;
			cmd.worldTM = tm;
			cmd.layer = layer;
			cmd.broadphase = 0;
			cmd.kinematic = kinematic;

			ps->CreateBody(cmd); // コマンドキューへ（固定Δtで一括適用）
		}
	}

	void SetContext(const ServiceLocator& serviceLocator) noexcept {
		ps = serviceLocator.Get<Physics::PhysicsService>();
		if (!ps) {
			LOG_ERROR("PhysicsService not found in BuildBodiesFromIntentsSystem");
			return;
		}
		reg = serviceLocator.Get<EntityManagerRegistry>();
		if (!reg) {
			LOG_ERROR("EntityManagerRegistry not found in BuildBodiesFromIntentsSystem");
			return;
		}
	}

	ECS::AccessInfo GetAccessInfo() const noexcept override {
		// 位置情報は EntityManager 内の locations を読むだけで、チャンク列は Read
		return ECS::ComponentAccess<ECS::Read<Physics::PhysicsInterpolation>, ECS::Read<Physics::BodyComponent>>::GetAccessInfo();
	}

private:
	ShapeResolverFn resolveShape;
	Physics::PhysicsService* ps = nullptr; // PhysicsService へのポインタ（サービスロケーターから取得）
	EntityManagerRegistry* reg = nullptr; // EntityManagerRegistry へのポインタ（サービスロケーターから取得）
};