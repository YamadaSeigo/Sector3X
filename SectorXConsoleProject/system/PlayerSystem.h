#pragma once


struct PlayerComponent
{
	bool isGround = false;

public:
	SPARSE_TAG
};

template<typename Partition>
class PlayerSystem : public ITypeSystem<
	PlayerSystem<Partition>,
	Partition,
	ComponentAccess<Write<CTransform>>,//アクセスするコンポーネントの指定
	ServiceContext<Physics::PhysicsService>>{//受け取るサービスの指定
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Physics::PhysicsService> physicsService) {
		ECS::EntityManager& globalEntityManager = partition.GetGlobalEntityManager();

		auto playerComponents = globalEntityManager.GetSparseComponents<PlayerComponent>();

		for (auto& player : playerComponents)
		{
			auto entityID = player.first;
			Physics::Mat34f outPose;
			physicsService->ReadCharacterPose(entityID, outPose);

			//位置と回転を反映
			auto* tf = globalEntityManager.GetComponent<CTransform>(entityID);
			if (tf != nullptr)
			{
				tf->location = outPose.pos;
				tf->rotation = outPose.rot;
			}
		}
	}
};
