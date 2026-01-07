#pragma once

#include "../app/PlayerService.h"
#include "../app/RenderDefine.h"

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
	PlayerSystem,
	Partition,
	ComponentAccess<Write<CTransform>>,//アクセスするコンポーネントの指定
	//受け取るサービスの指定
	ServiceContext<
		Physics::PhysicsService,
		Graphics::I3DPerCameraService,
		Graphics::RenderService,
		InputService,
		PlayerService,
		Audio::AudioService
	>>{

public:
	static inline const Math::Vec3f cameraOffset = {0.0f,3.0f,0.0f}; // 移動速度（m/s）

	// 入力から希望する速度を計算する（仮実装）
	Math::Vec3f CalcWishVelocityFromInput(
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService,
		NoDeletePtr<InputService> inputService)
	{
		Math::Vec3f wishVelocity;
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
			if (inputService->IsKeyPressed(Input::Key::Space)){
				wishVelocity = PlayerService::GRAVITY * -PlayerService::HOVER_POWER;
			}
		}

		if (inputDir.lengthSquared() > 0.0f) {
			inputDir = inputDir.normalized();
		}
		else
		{
			return wishVelocity;
		}

		auto camForward = cameraService->GetForward();
		Math::Vec3f playerRight = PlayerService::UP.cross(camForward).normalized();
		Math::Vec3f playerForward = playerRight.cross(PlayerService::UP).normalized();

		float boostBias = inputService->IsKeyPressed(Input::Key::LShift) ? PlayerService::BOOST_POWER : 1.0f;

		wishVelocity +=
			playerRight * inputDir.x * PlayerService::MOVE_SPEED * boostBias + // 横移動速度
			playerForward * inputDir.y * PlayerService::MOVE_SPEED * boostBias; // 前後移動速度

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
	void StartImpl(
		NoDeletePtr<Physics::PhysicsService> physicsService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<PlayerService> playerService,
		NoDeletePtr<Audio::AudioService> audioService)
	{
		grassStepHandle = audioService->EnqueueLoadWav("assets/audio/SE/walk-on-grass.wav");
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<Physics::PhysicsService> physicsService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<PlayerService> playerService,
		NoDeletePtr<Audio::AudioService> audioService)
	{
		ECS::EntityManager& globalEntityManager = partition.GetGlobalEntityManager();

		auto playerComponents = globalEntityManager.GetSparseComponents<PlayerComponent>();
		float dt = static_cast<float>(physicsService->GetDeltaTime());

		static bool playerCamera = true;
		if (inputService->IsKeyTrigger(Input::Key::F1))
		{
			playerCamera = !playerCamera;

			using RotateMode = Graphics::I3DPerCameraService::RotateMode;

			cameraService->SetRotateMode(playerCamera ? RotateMode::Orbital : RotateMode::FPS);
		}

		static std::random_device rd;
		static std::mt19937_64 rng(rd());

		static std::uniform_real_distribution<float> distVolume(0.2f, 0.4f);
		static std::uniform_real_distribution<float> distPitch(0.75f, 1.0f);
		static std::uniform_real_distribution <float> distDelay(4.0f, 5.0f);

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


				static float stepSoundDelay = 0.0f;

				float wishSquared = wish.lengthSquared();
				bool move = wishSquared > 0.0f;
				if (move) {
					// 向きを変える
					targetYaw = std::atan2(wish.x, wish.z);

					stepSoundDelay -= dt * (2.0f + std::sqrt(wishSquared) * 0.2f); // フレーム数換算
					if (stepSoundDelay < 0.0f)
						stepSoundDelay = 0.0f;
				}
				else
				{
					// 止まっているときは歩行音をリセット
					stepSoundDelay = 0.0f;
				}

				if (comp.isGrounded) {

					// 地面にいるので縦成分を消す＆坂で滑らせない
					float vy = v.dot(PlayerService::UP);
					v -= PlayerService::UP * vy;

					if (move)
					{
						// 歩行音
						if (stepSoundDelay <= 0.0f)
						{
							Audio::AudioPlayParams params;
							params.volume = distVolume(rng);
							params.pitch = distPitch(rng);
							audioService->EnqueuePlay(grassStepHandle, params);
							stepSoundDelay = distDelay(rng);
						}
					}
				}
				else {
					// 空中：自前で重力を足す
					v += PlayerService::GRAVITY * dt;
				}

				v.x = wish.x;
				v.z = wish.z;

				comp.currentVelocity = v;

				v.y += wish.y;

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
				using namespace Physics;

				auto pose = physicsService->ReadCharacterPose(entityID);
				if (!pose.has_value()) continue;

				auto playerPos = pose.value().GetPosition();
				auto playerRot = pose.value().GetRotation();

				auto modelComp = globalEntityManager.ReadComponent<CModel>(entityID);

				if (modelComp.has_value())
				{
					auto WorldMtx = Math::MakeTranslationMatrix(playerPos) * Math::MakeRotationMatrix(playerRot);
					auto session = renderService->GetProducerSession(PassGroupName[GROUP_3D_MAIN]);
					auto instanceIdx = session.AllocInstance(WorldMtx);

					auto modelMgr = renderService->GetResourceManager<Graphics::DX11::ModelAssetManager>();
					auto modelData = modelMgr->Get(modelComp.value().handle);
					for (const auto& subMesh : modelData.ref().subMeshes)
					{
						Graphics::DrawCommand cmd;
						cmd.sortKey = 0;
						cmd.instanceIndex = instanceIdx;
						cmd.overridePSO = subMesh.overridePSO.index;
						cmd.mesh = subMesh.lods[0].mesh.index;
						cmd.material = subMesh.material.index;
						cmd.viewMask = PASS_3DMAIN_HIGHLIGHT;
						session.Push(std::move(cmd));
					}
				}

				if (playerCamera)
				{
					// カメラに見てほしい理想位置（少し頭の上とか）
					const Math::Vec3f desiredTarget = playerPos + cameraOffset;

					static FollowCamState camState;

					if (!camState.initialized) {
						camState.smoothedTarget = desiredTarget;
						camState.initialized = true;
					}

					// === Lerp ベースのスムージング ===
					const float followSpeed = 6.0f; // 値を大きくすると早く追従、小さくするとヌルヌル

					// フレームレートにそこそこ強い書き方
					float alpha = 1.0f - std::exp(-followSpeed * dt);  // 0..1
					camState.smoothedTarget += (desiredTarget - camState.smoothedTarget) * alpha;

					// カメラには「スムージング後のターゲット」を渡す
					cameraService->SetTarget(camState.smoothedTarget);

					static float cameraDistance = cameraService->GetFocusDistance();

					static int prevCameraHit = 0;

					int mouseWheelV, mouseWheelH;
					inputService->GetMouseWheel(mouseWheelV, mouseWheelH);

					if (mouseWheelV != 0)
						cameraDistance -= mouseWheelV * 0.5f;

					RayCastCmd rayCmd;
					rayCmd.requestId = player.first.index;
					rayCmd.broadPhaseMask = MakeBPMask(Layers::BPLayers::NON_MOVING) | MakeBPMask(Layers::BPLayers::MOVING);
					rayCmd.objectLayerMask = MakeObjectLayerMask(Layers::NON_MOVING_RAY_HIT);
					rayCmd.origin = camState.smoothedTarget;

					//最新のカメラの向きを計算して求める(Rayの結果取得に一フレーム遅延があるため)
					auto camRot = cameraService->CalcCurrentRotation();
					auto basis = Math::FastBasisFromQuat(camRot);

					rayCmd.dir = basis.forward * -1.0f; // カメラの後ろ方向
					rayCmd.maxDist = cameraDistance;

					float focusDist = cameraDistance;
					physicsService->RayCast(rayCmd);

					const auto& snapshot = physicsService->CurrentSnapshot();
					for (const auto& rayHit : snapshot.rayHits)
					{
						if (rayHit.requestId == rayCmd.requestId)
						{
							if (rayHit.hit)
							{
								float focusDist = (std::max)(rayHit.distance - 1.0f, 1.0f); // 少し手前に
								cameraService->SetFocusDistance(focusDist);
								prevCameraHit = 2;
							}
							else if(prevCameraHit > 0)
							{
								prevCameraHit--;
							}
							else
							{
								cameraService->SetFocusDistance(cameraDistance);
							}
							break;
						}
					}


					long dx, dy;
					inputService->GetMouseDelta(dx, dy);
					cameraService->SetMouseDelta(static_cast<float>(dx), static_cast<float>(dy));
				}


				//足元の位置をPlayerServiceにセット
				playerService->SetFootData(playerPos);
			
				//PlayerServiceにも位置をセット
				playerService->SetPlayerPosition(playerPos);

				//位置と回転を反映
				globalEntityManager.ReadWriteComponent<CTransform>(entityID,
					[&](CTransform tf) {
						tf.location = playerPos;
						tf.rotation = playerRot;

						return tf;
					});

				comp.isGrounded = (pose.value().GetGroundState() == Physics::CharacterPose::GroundState::OnGround);
			}
		}
	}

	void EndImpl(Partition& partition,
		NoDeletePtr<Physics::PhysicsService> physicsService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<PlayerService> playerService,
		NoDeletePtr<Audio::AudioService> audioService)
	{
		ECS::EntityManager& globalEntityManager = partition.GetGlobalEntityManager();

		auto playerComponents = globalEntityManager.GetSparseComponents<PlayerComponent>();

		for (auto& player : playerComponents)
		{
			auto entityID = player.first;

			// キャラクターコントローラー削除
			physicsService->DestroyCharacter(entityID);
		}
	}

private:
	Audio::SoundHandle grassStepHandle = Audio::SoundHandle{ 0 };
	Audio::AudioTicketID grassStepTicket = Audio::AudioTicketID::Invalid();
};
