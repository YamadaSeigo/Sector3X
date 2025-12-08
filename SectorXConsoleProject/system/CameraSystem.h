#pragma once

template<typename Partition>
class CameraSystem : public ITypeSystem<
	CameraSystem<Partition>,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<>,
	//受け取るサービスの指定
	ServiceContext<
		InputService,
		Graphics::I3DPerCameraService,
		Graphics::I2DCameraService,
		Graphics::LightShadowService>
	>
{
	//using Accessor = ComponentAccessor<>;

	static constexpr float MOVE_SPEED_WHEEL_RATE = 0.5f;
public:
	void StartImpl(
		UndeletablePtr<InputService> inputService,
		UndeletablePtr<Graphics::I3DPerCameraService> perCameraService,
		UndeletablePtr<Graphics::I2DCameraService> camera2DService,
		UndeletablePtr<Graphics::LightShadowService> lightShadowService
	)
	{
		Graphics::CameraParams camParams;
		camParams.view = perCameraService->MakeViewMatrix();
		camParams.position = perCameraService->GetEyePos();
		camParams.nearPlane = perCameraService->GetNearClip();
		camParams.farPlane = perCameraService->GetFarClip();
		camParams.fovY = perCameraService->GetFOV();
		camParams.aspect = perCameraService->GetAspectRatio();

		lightShadowService->UpdateCascade(camParams, cascadeSceneAABB);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<InputService> inputService,
		UndeletablePtr<Graphics::I3DPerCameraService> perCameraService,
		UndeletablePtr<Graphics::I2DCameraService> camera2DService,
		UndeletablePtr<Graphics::LightShadowService> lightShadowService) {
		int mouseWheelV, mouseWheelH;
		inputService->GetMouseWheel(mouseWheelV, mouseWheelH);

		if (inputService->IsRButtonPressed()) {

			perCameraService->SetRotateMode(Graphics::I3DPerCameraService::RotateMode::FPS);

			if (inputService->IsKeyPressed(Input::Key::E)) {
				perCameraService->Move(Math::LFAxes::up() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::Q)) {
				perCameraService->Move(Math::LFAxes::down() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::W)) {
				perCameraService->Move(perCameraService->GetForward() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::A)) {
				perCameraService->Move(perCameraService->GetRight() * -moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::S)) {
				perCameraService->Move(perCameraService->GetForward() * -moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::D)) {
				perCameraService->Move(perCameraService->GetRight() * moveSpeed);
			}
		}
		else
		{
			//perCameraService->SetRotateMode(Graphics::I3DPerCameraService::RotateMode::Orbital);

			if (mouseWheelV != 0) {
				perCameraService->SetFocusDistance(
					perCameraService->GetFocusDistance() - (float)mouseWheelV * 0.5f);
				camera2DService->Zoom((float)mouseWheelV);
			}
		}


		if (inputService->IsMouseCaptured()) {
			long dx, dy;
			inputService->GetMouseDelta(dx, dy);
			perCameraService->SetMouseDelta(static_cast<float>(dx), static_cast<float>(dy));

			moveSpeed = std::clamp(moveSpeed + (float)mouseWheelV * MOVE_SPEED_WHEEL_RATE * (std::max)(1.0f, moveSpeed / 20.0f), 0.1f, 200.0f);
		}

		bool updateCascade = false;
		//方向ライトの回転（本来はカメラシステムでやるべきではない。デバッグのためにとりあえず）
		if (inputService->IsKeyPressed(Input::Key::L))
		{
			const float lightRotateSpeed = Math::Deg2Rad(1.0f);

			auto dirLight = lightShadowService->GetDirectionalLight();

			if (inputService->IsKeyPressed(Input::Key::Left))
			{
				updateCascade = true;

				auto dir = dirLight.directionWS;

				dirLight.directionWS = {
					cos(lightRotateSpeed) * dir.x - sin(lightRotateSpeed) * dir.z,
					dirLight.directionWS.y,
					sin(lightRotateSpeed) * dir.x + cos(lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Right))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					cos(-lightRotateSpeed) * dir.x - sin(-lightRotateSpeed) * dir.z,
					dirLight.directionWS.y,
					sin(-lightRotateSpeed) * dir.x + cos(-lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Up))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					dirLight.directionWS.x,
					cos(lightRotateSpeed) * dir.y - sin(lightRotateSpeed) * dir.z,
					sin(lightRotateSpeed) * dir.y + cos(lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Down))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					dirLight.directionWS.x,
					cos(-lightRotateSpeed) * dir.y - sin(-lightRotateSpeed) * dir.z,
					sin(-lightRotateSpeed) * dir.y + cos(-lightRotateSpeed) * dir.z,
				};
			}

			lightShadowService->SetDirectionalLight(dirLight);
		}

		if (perCameraService->IsUpdateBuffer() || updateCascade)
		{
			Graphics::CameraParams camParams;
			camParams.view = perCameraService->MakeViewMatrix();
			camParams.position = perCameraService->GetEyePos();
			camParams.nearPlane = perCameraService->GetNearClip();
			camParams.farPlane = perCameraService->GetFarClip();
			camParams.fovY = perCameraService->GetFOV();
			camParams.aspect = perCameraService->GetAspectRatio();

			lightShadowService->UpdateCascade(camParams, cascadeSceneAABB);
		}
	}
private:
	float moveSpeed = 1.0f;
	Math::AABB3f cascadeSceneAABB = { {0,-500.0f,0},{5000.0f,500.0f,5000.0f} };
};
