// BodyIDWriteBackFromEventsSystem.hpp
#pragma once
#include <limits>

#include <SectorFW/Core/ECS/ISystem.hpp>
#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Physics/PhysicsService.h>
#include <SectorFW/Core/EntityManagerRegistryService.h>

// PhysicsDevice が溜めた「作成完了イベント」だけをドレインして BodyID を差し込む。
template<class Partition>
class BodyIDWriteBackFromEventsSystem final : public ECS::ISystem<Partition> {
public:
	void Update(Partition& /*p*/, const ECS::ServiceLocator& S) override {
		using namespace SectorFW::Physics;

		std::vector<PhysicsService::CreatedBody> evs;
		ps->ConsumeCreatedBodies(evs);
		if (evs.empty()) return;

		for (const auto& ev : evs) {
			ECS::EntityManager* owner = reg->ResolveOwner(ev.owner);
			if (!owner) continue;
			auto loc = owner->TryGetLocation(ev.e);
			if (!loc) continue; // 既に消滅 or 移籍

			ECS::ArchetypeChunk* ch = loc->chunk;
			size_t row = loc->index;
			if (row >= ch->GetEntityCount()) continue;

			ComponentAccessor<ECS::Write<BodyComponent>> chAccessor(ch);

			auto bodyCol = chAccessor.Get<Write<BodyComponent>>();
			if (!bodyCol) continue;

			auto& bodyValue = bodyCol.value().body()[row];
			// Pending センチネル（0xFFFFFFFF）に限って上書き（多重作成防止）
			if (bodyValue.GetIndexAndSequenceNumber() == (std::numeric_limits<uint32_t>::max)()) {
				bodyValue = ev.id; // 差し込み完了 → Alive
			}
		}
	}

	void SetContext(const ServiceLocator& serviceLocator) noexcept {
		ps = serviceLocator.Get<Physics::PhysicsService>(); if (!ps) {
			LOG_ERROR("PhysicsService not found in BodyIDWriteBackFromEventsSystem");
			return;
		}
		reg = serviceLocator.Get<EntityManagerRegistry>(); if (!reg) {
			LOG_ERROR("EntityManagerRegistry not found in BodyIDWriteBackFromEventsSystem");
			return;
		}
	}

	ECS::AccessInfo GetAccessInfo() const noexcept override {
		return ECS::ComponentAccess<ECS::Write<Physics::BodyComponent>>::GetAccessInfo();
	}
private:
	Physics::PhysicsService* ps = nullptr;
	EntityManagerRegistry* reg = nullptr;
};