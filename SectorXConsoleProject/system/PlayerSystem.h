#pragma once


struct PlayerComponent
{
	Math::Vec3f currentVelocity;
	bool isGrounded = false;
public:
	SPARSE_TAG
};

template<typename Partition>
class PlayerSystem : public ITypeSystem<
	PlayerSystem<Partition>,
	Partition,
	ComponentAccess<Write<CTransform>>,//アクセスするコンポーネントの指定
	//受け取るサービスの指定
	ServiceContext<
		Physics::PhysicsService,
		Graphics::I3DPerCameraService,
		InputService
	>>{

public:
	static inline Math::Vec3f gravity = Math::Vec3f(0.0f, -9.81f, 0.0f);
	static inline Math::Vec3f up = Math::Vec3f(0.0f, 1.0f, 0.0f);

	static inline float moveSpeed = 5.0f;

	// 入力から希望する速度を計算する（仮実装）
	Math::Vec3f CalcWishVelocityFromInput(
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService,
		UndeletablePtr<InputService> inputService
	)
	{
		Math::Vec2f inputDir(0.0f, 0.0f);

		if (inputService->IsMouseCaptured() && !inputService->IsRButtonPressed())
		{
			if (inputService->IsKeyPressed(Input::Key::W)) {
				inputDir.y += 1.0f;
			}
			if (inputService->IsKeyPressed(Input::Key::S)) {
				inputDir.y -= 1.0f;
			}
			if (inputService->IsKeyPressed(Input::Key::A)) {
				inputDir.x -= 1.0f;
			}
			if (inputService->IsKeyPressed(Input::Key::D)) {
				inputDir.x += 1.0f;
			}
		}

		if (inputDir.length() > 0.0f) {
			inputDir = inputDir.normalized();
		}
		else
		{
			return Math::Vec3f(0.0f, 0.0f, 0.0f);
		}

		auto camForward = cameraService->GetForward();
		Math::Vec3f playerRight = up.cross(camForward).normalized();
		Math::Vec3f playerForward = playerRight.cross(up).normalized();

		Math::Vec3f wishVelocity =
			playerRight * inputDir.x * moveSpeed + // 横移動速度
			playerForward * inputDir.y * moveSpeed; // 前後移動速度

		return wishVelocity;
	}

	void StartImpl(
		UndeletablePtr<Physics::PhysicsService> physicsService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService,
		UndeletablePtr<InputService> inputService)
	{
		//初期化処理
		//imguiデバッグ用スライダー登録
		BIND_DEBUG_SLIDER_FLOAT("Player", "MoveSpeed", &moveSpeed, 0.0f, 10.0f, 0.1f);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<Physics::PhysicsService> physicsService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService,
		UndeletablePtr<InputService> inputService)
	{
		ECS::EntityManager& globalEntityManager = partition.GetGlobalEntityManager();

		auto playerComponents = globalEntityManager.GetSparseComponents<PlayerComponent>();

		for (auto& player : playerComponents)
		{
			auto entityID = player.first;
			auto& comp = player.second;

			//Velocity更新
			{
				// 前フレーム保存しておいた currentVelocity をベースに
				Math::Vec3f v = comp.currentVelocity;

				if (comp.isGrounded) {

					// 地面にいるので縦成分を消す＆坂で滑らせない
					float vy = v.dot(up);
					v -= up * vy;

					// 入力から水平速度を決める
					Math::Vec3f wish = CalcWishVelocityFromInput(cameraService, inputService);
					v.x = wish.x;
					v.z = wish.z;
				}
				else {
					// 空中：自前で重力を足す
					v += gravity * physicsService->GetDeltaTime();
					// 空中制御が欲しいなら XZ だけ補正
					Math::Vec3f wish = CalcWishVelocityFromInput(cameraService, inputService);
					v.x = wish.x;
					v.z = wish.z;
				}

				comp.currentVelocity = v;

				physicsService->SetCharacterVelocity(entityID, v);//速度をリセット
			}

			//Pose読み込み & 反映
			{
				auto pose = physicsService->ReadCharacterPose(entityID);
				if (!pose.has_value()) continue;

				//位置と回転を反映
				globalEntityManager.ReadWriteComponent<CTransform>(entityID,
					[&](CTransform tf) {
						tf.location = pose.value().GetPosition();
						tf.rotation = pose.value().GetRotation();

						return tf;
					});

				comp.isGrounded = (pose.value().GetGroundState() != Physics::CharacterPose::GroundState::InAir);
			}
		}
	}
};
