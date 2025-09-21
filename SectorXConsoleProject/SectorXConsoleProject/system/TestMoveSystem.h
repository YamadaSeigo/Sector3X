#pragma once

template<typename Partition>
class TestMoveSystem : public ITypeSystem<
	TestMoveSystem<Partition>,
	Partition,
	ComponentAccess<Write<CTransform>, Write<SpatialMotionTag>>,//アクセスするコンポーネントの指定
	ServiceContext<SpatialChunkRegistry>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Write<CTransform>, Write<SpatialMotionTag>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, LevelContext& levelCtx, UndeletablePtr<SpatialChunkRegistry> registry) {

		BudgetMover::LocalBatch moveBatch(levelCtx.mover, 200);

		this->ForEachChunkWithAccessorAndEntityIDs([](Accessor& accessor, size_t entityCount,
			const std::vector<EntityID>& ids, auto* partition, auto* registry, auto* levelCtx, auto* moveBatch)
			{
				auto transform = accessor.Get<Write<CTransform>>();
				if (!transform) { LOG_ERROR("Transform component not found in VoidSystem"); return; }

				auto tagPtr = accessor.Get<Write<SpatialMotionTag>>();
				if (!tagPtr) { LOG_ERROR("SpatialMotionTag component not found in VoidSystem"); return; }

				for (size_t i = 0; i < entityCount; ++i) {
					transform->px()[i] += 10.0f / 60.0f;
					//transform->py()[i] += 0.5f / 60.0f;
					//transform->pz()[i] += 0.25f / 60.0f;

					auto& tag = tagPtr.value()[i];

					Math::Vec3f newPos = {
						transform->px()[i],
						transform->py()[i],
						transform->pz()[i]
					};

					MoveIfCrossed_Deferred(ids[i], newPos, *partition, *registry, levelCtx->GetID(), tag.handle, *moveBatch);
				}
			}, partition, &partition, registry.get(), &levelCtx, &moveBatch);
	}
};
