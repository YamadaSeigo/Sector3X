#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include <SectorFW/Math/sx_math.h>
#include <SectorFW/SIMD/simd_api.h>


//#define USE_LERP_SIMD

template<typename Partition>
class PhysicsSystem : public ITypeSystem<
	PhysicsSystem<Partition>,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Write<TransformSoA>,
		Write<Physics::PhysicsInterpolation>,
		Read<Physics::CPhyBody>,
		Write<SpatialMotionTag>
	>,
	//受け取るサービスの指定
	ServiceContext<
		Physics::PhysicsService,
		SpatialChunkRegistry>
	>
{
	using Accessor = ComponentAccessor<Write<TransformSoA>, Write<Physics::PhysicsInterpolation>, Read<Physics::CPhyBody>, Write<SpatialMotionTag>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, LevelContext<Partition>& levelCtx, safe_ptr<Physics::PhysicsService> physicsService,
		safe_ptr<SpatialChunkRegistry> chunkReg) {

		BudgetMover::LocalBatch moveBatch(levelCtx.mover, 200);

		this->ForEachChunkWithAccessorAndEntityIDs([](Accessor& accessor, size_t entityCount,
			const std::vector<ECS::EntityID>& entityIDs, Physics::PhysicsService* physics,
			SpatialChunkRegistry* registry, Partition* partition, LevelContext* levelCtx, auto* moveBatch
			)
			{
				auto transform = accessor.Get<Write<TransformSoA>>();
				if (!transform) [[unlikely]] { LOG_ERROR("TransformSoA component not found in PhysicsSystem"); return; }

				auto interpolation = accessor.Get<Write<Physics::PhysicsInterpolation>>();
				if (!interpolation) [[unlikely]] { LOG_ERROR("PhysicsInterpolation component not found in PhysicsSystem"); return; }

				auto bodyComponent = accessor.Get<Read<Physics::CPhyBody>>();
				if (!bodyComponent) [[unlikely]] { LOG_ERROR("BodyComponent not found in PhysicsSystem"); return; }

				auto motionTag = accessor.Get<Write<SpatialMotionTag>>();
				if (!motionTag) [[unlikely]] { LOG_ERROR("SpatialMotionTag component not found in PhysicsSystem"); return; }

				size_t size = sizeof(float) * entityCount;
				memcpy(interpolation->ppx(), interpolation->cpx(), size);
				memcpy(interpolation->ppy(), interpolation->cpy(), size);
				memcpy(interpolation->ppz(), interpolation->cpz(), size);
				memcpy(interpolation->prx(), interpolation->crx(), size);
				memcpy(interpolation->pry(), interpolation->cry(), size);
				memcpy(interpolation->prz(), interpolation->crz(), size);
				memcpy(interpolation->prw(), interpolation->crw(), size);

				// 未初期化の連続したメモリ動的確保。自動的にメモリは破棄される
				auto updateMasks = std::make_unique_for_overwrite<uint32_t[]>(entityCount);

				Physics::PoseBatchView poseBatch = {
					interpolation->cpx(),interpolation->cpy(),interpolation->cpz(),
					interpolation->crx(), interpolation->cry(), interpolation->crz(), interpolation->crw(),
					updateMasks.get(), entityCount, bodyComponent->body(), (uint8_t*)bodyComponent->type()
				};

				physics->BuildPoseBatch(poseBatch);

				auto alpha = physics->GetAlpha();

				using namespace SFW::SIMD;

				// 位置
#ifdef USE_LERP_SIMD
				gUpdateScalarLerp(transform->px(), interpolation->ppx(), interpolation->cpx(), updateMasks.get(), entityCount, alpha);
				gUpdateScalarLerp(transform->py(), interpolation->ppy(), interpolation->cpy(), updateMasks.get(), entityCount, alpha);
				gUpdateScalarLerp(transform->pz(), interpolation->ppz(), interpolation->cpz(), updateMasks.get(), entityCount, alpha);

				// 回転（最短経路 nlerp）
				gUpdateQuatNlerpShortest(
					transform->qx(), transform->qy(), transform->qz(), transform->qw(),
					interpolation->prx(), interpolation->pry(), interpolation->prz(), interpolation->prw(),
					interpolation->crx(), interpolation->cry(), interpolation->crz(), interpolation->crw(),
					updateMasks.get(), entityCount, alpha);
#else
				for (size_t i = 0; i < entityCount; ++i)
				{
					if (updateMasks[i] == 0) continue;

					// 位置
					transform->px()[i] = interpolation->ppx()[i] + (interpolation->cpx()[i] - interpolation->ppx()[i]) * alpha;
					transform->py()[i] = interpolation->ppy()[i] + (interpolation->cpy()[i] - interpolation->ppy()[i]) * alpha;
					transform->pz()[i] = interpolation->ppz()[i] + (interpolation->cpz()[i] - interpolation->ppz()[i]) * alpha;
					// 回転（最短経路 nlerp）
					Math::Quatf prevQ{ interpolation->prx()[i], interpolation->pry()[i], interpolation->prz()[i], interpolation->prw()[i] };
					Math::Quatf currQ{ interpolation->crx()[i], interpolation->cry()[i], interpolation->crz()[i], interpolation->crw()[i] };
					Math::Quatf nlerpQ = Math::Quatf::Slerp(prevQ, currQ, alpha);
					transform->qx()[i] = nlerpQ.x;
					transform->qy()[i] = nlerpQ.y;
					transform->qz()[i] = nlerpQ.z;
					transform->qw()[i] = nlerpQ.w;

					Math::Vec3f newPos = { transform->px()[i], transform->py()[i], transform->pz()[i] };
					Math::Vec3f vel = {
						(interpolation->cpx()[i] - interpolation->ppx()[i]) / (1.0f / 60.0f),
						(interpolation->cpy()[i] - interpolation->ppy()[i]) / (1.0f / 60.0f),
						(interpolation->cpz()[i] - interpolation->ppz()[i]) / (1.0f / 60.0f) };

					// 1) 退避運用
					SpatialMotionTag& tag = (*motionTag)[i];
					//constexpr SettleRule rule{ 0.3f, 6 }; // 速度0.3以下を6フレームで再アタッチ
					//UpdateSpatialAttachment(
					//	entityIDs[i],
					//	newPos,
					//	vel,
					//	*partition,
					//	*registry,
					//	levelCtx->id,
					//	tag,
					//	partition->GetGlobalEntityManager(),
					//	rule);

					MoveIfCrossed_Deferred(entityIDs[i], newPos, *partition, *registry, levelCtx->GetID(), tag.handle, *moveBatch);
				}
#endif // USE_LERP_SIMD

			}, partition, physicsService.get(), chunkReg.get(), &partition, &levelCtx, &moveBatch);
	}
};
