#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Math/sx_math.h>

template<typename Partition>
class PhysicsSystem : public ITypeSystem<
	PhysicsSystem<Partition>,
	Partition,
	ComponentAccess<Write<TransformSoA>, Write<Physics::PhysicsInterpolation>, Read<Physics::BodyComponent>>,//アクセスするコンポーネントの指定
	ServiceContext<Physics::PhysicsService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Write<TransformSoA>, Write<Physics::PhysicsInterpolation>, Read<Physics::BodyComponent>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Physics::PhysicsService> physicsService) {
		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount, Physics::PhysicsService* physics)
			{
				auto transform = accessor.Get<Write<TransformSoA>>();
				if (!transform) { LOG_ERROR("TransformSoA component not found in PhysicsSystem"); return; }

				auto interpolation = accessor.Get<Write<Physics::PhysicsInterpolation>>();
				if (!interpolation) { LOG_ERROR("PhysicsInterpolation component not found in PhysicsSystem"); return; }

				auto bodyComponent = accessor.Get<Read<Physics::BodyComponent>>();
				if (!bodyComponent) { LOG_ERROR("BodyComponent not found in PhysicsSystem"); return; }

				size_t size = sizeof(float) * entityCount;
				memcpy(interpolation->ppx(), interpolation->cpx(), size);
				memcpy(interpolation->ppy(), interpolation->cpy(), size);
				memcpy(interpolation->ppz(), interpolation->cpz(), size);
				memcpy(interpolation->prx(), interpolation->crx(), size);
				memcpy(interpolation->pry(), interpolation->cry(), size);
				memcpy(interpolation->prz(), interpolation->crz(), size);
				memcpy(interpolation->prw(), interpolation->crw(), size);

				std::vector<uint8_t> updateMasks(entityCount, 0);
				Physics::PoseBatchView poseBatch = {
					interpolation->cpx(),interpolation->cpy(),interpolation->cpz(),
					interpolation->crx(), interpolation->cry(), interpolation->crz(), interpolation->crw(),
					updateMasks.data(), entityCount, bodyComponent->body(), bodyComponent->isStatic()
				};

				physics->BuildPoseBatch(poseBatch);

				//固定->可変にするため線形補間
				auto alpha = physics->GetAlpha();
				for (size_t i = 0; i < entityCount; ++i) {
					if (updateMasks[i] == 1) {
						// 位置と回転を更新
						transform->px()[i] = Math::lerp(interpolation->ppx()[i], interpolation->cpx()[i], alpha);
						transform->py()[i] = Math::lerp(interpolation->ppy()[i], interpolation->cpy()[i], alpha);
						transform->pz()[i] = Math::lerp(interpolation->ppz()[i], interpolation->cpz()[i], alpha);
						transform->qx()[i] = Math::lerp(interpolation->prx()[i], interpolation->crx()[i], alpha);
						transform->qy()[i] = Math::lerp(interpolation->pry()[i], interpolation->cry()[i], alpha);
						transform->qz()[i] = Math::lerp(interpolation->prz()[i], interpolation->crz()[i], alpha);
						transform->qw()[i] = Math::lerp(interpolation->prw()[i], interpolation->crw()[i], alpha);
					}
				}
			}, partition, physicsService.get());
	}
};
