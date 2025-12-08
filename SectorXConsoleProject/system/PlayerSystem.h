#pragma once

#include "../app/PlayerService.h"

struct PlayerComponent
{
	Math::Vec3f currentVelocity;
	float yaw = 0.0f;
	bool isGrounded = false;
public:
	SPARSE_TAG
};

// プレイヤー制御システム
template<typename Partition>
class PlayerSystem : public ITypeSystem<
	PlayerSystem<Partition>,
	Partition,
	ComponentAccess<Write<CTransform>>,//アクセスするコンポーネントの指定
	//受け取るサービスの指定
	ServiceContext<
		Physics::PhysicsService,
		Graphics::I3DPerCameraService,
		InputService,
		PlayerService
	>>{

public:
	static inline const Math::Vec3f cameraOffset = {0.0f,3.0f,0.0f}; // 移動速度（m/s）

	// 入力から希望する速度を計算する（仮実装）
	Math::Vec3f CalcWishVelocityFromInput(
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService,
		UndeletablePtr<InputService> inputService)
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

		if (inputDir.lengthSquared() > 0.0f) {
			inputDir = inputDir.normalized();
		}
		else
		{
			return Math::Vec3f(0.0f, 0.0f, 0.0f);
		}

		auto camForward = cameraService->GetForward();
		Math::Vec3f playerRight = PlayerService::UP.cross(camForward).normalized();
		Math::Vec3f playerForward = playerRight.cross(PlayerService::UP).normalized();

		Math::Vec3f wishVelocity =
			playerRight * inputDir.x * PlayerService::MOVE_SPEED + // 横移動速度
			playerForward * inputDir.y * PlayerService::MOVE_SPEED; // 前後移動速度

		return wishVelocity;
	}

	float WrapAngle(float a) {
		// [-π, π] に正規化
		a = std::fmod(a + Math::pi_v<float>, Math::tau_v<float>);
		if (a < 0.0f) a += Math::tau_v<float>;
		return a - Math::pi_v<float>;
	}

	// current から見て target への「最短の差角」を返す
	float ShortestAngleDiff(float current, float target) {
		float diff = target - current;
		return WrapAngle(diff); // 結果は [-π, π]
	}

	struct FollowCamState
	{
		Math::Vec3f smoothedTarget;
		bool initialized = false;
	};

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<Physics::PhysicsService> physicsService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService,
		UndeletablePtr<InputService> inputService,
		UndeletablePtr<PlayerService> playerService)
	{
		ECS::EntityManager& globalEntityManager = partition.GetGlobalEntityManager();

		auto playerComponents = globalEntityManager.GetSparseComponents<PlayerComponent>();
		float dt = physicsService->GetDeltaTime();

		for (auto& player : playerComponents)
		{
			auto entityID = player.first;
			auto& comp = player.second;

			//Velocity更新
			{
				// 前フレーム保存しておいた currentVelocity をベースに
				Math::Vec3f v = comp.currentVelocity;
				float currentYaw = comp.yaw;
				float targetYaw = currentYaw;

				// 空中制御が欲しいなら XZ だけ補正
				Math::Vec3f wish = CalcWishVelocityFromInput(cameraService, inputService);

				if (wish.lengthSquared() > 0.0f) {
					// 向きを変える
					targetYaw = std::atan2(wish.x, wish.z);
				}

				if (comp.isGrounded) {

					// 地面にいるので縦成分を消す＆坂で滑らせない
					float vy = v.dot(PlayerService::UP);
					v -= PlayerService::UP * vy;
				}
				else {
					// 空中：自前で重力を足す
					v += PlayerService::GRAVITY * dt;
				}

				v.x = wish.x;
				v.z = wish.z;

				comp.currentVelocity = v;

				float diff = ShortestAngleDiff(currentYaw, targetYaw); // [-π, π]

				// このフレームで回していい最大角度
				float maxStep = PlayerService::TURN_SPEED * dt; // rad/s × 秒

				// 実際に回す量をクランプ
				diff = std::clamp(diff, -maxStep, maxStep);

				// 1フレーム分だけ回転
				float newYaw = currentYaw + diff;

				comp.yaw = newYaw;

				physicsService->SetCharacterVelocity(entityID, v);//速度をリセット

				Math::Quatf newYawQuat = Math::Quatf::FromAxisAngle(PlayerService::UP, newYaw);
				physicsService->SetCharacterRotation(entityID, newYawQuat);//回転をリセット
			}

			//Pose読み込み & 反映
			{
				auto pose = physicsService->ReadCharacterPose(entityID);
				if (!pose.has_value()) continue;

				auto playerPos = pose.value().GetPosition();

				if (!inputService->IsRButtonPressed())
				{
					// カメラに見てほしい理想位置（少し頭の上とか）
					const Math::Vec3f desiredTarget = playerPos + cameraOffset;

					static FollowCamState camState;

					if (!camState.initialized) {
						camState.smoothedTarget = desiredTarget;
						camState.initialized = true;
					}

					// === Lerp ベースのスムージング ===
					const float followSpeed = 8.0f; // 値を大きくすると早く追従、小さくするとヌルヌル

					// フレームレートにそこそこ強い書き方
					float alpha = 1.0f - std::exp(-followSpeed * dt);  // 0..1
					camState.smoothedTarget += (desiredTarget - camState.smoothedTarget) * alpha;

					// カメラには「スムージング後のターゲット」を渡す
					cameraService->SetTarget(camState.smoothedTarget);
				}


				//足元の位置をPlayerServiceにセット
				playerService->SetFootData(playerPos);

				//位置と回転を反映
				globalEntityManager.ReadWriteComponent<CTransform>(entityID,
					[&](CTransform tf) {
						tf.location = playerPos;
						tf.rotation = pose.value().GetRotation();

						return tf;
					});

				comp.isGrounded = (pose.value().GetGroundState() == Physics::CharacterPose::GroundState::OnGround);
			}
		}
	}
};
