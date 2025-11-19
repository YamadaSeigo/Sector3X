#pragma once

template<typename Partition>
class CameraSystem : public ITypeSystem<
	CameraSystem<Partition>,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<InputService, Graphics::I3DPerCameraService, Graphics::I2DCameraService, Graphics::LightShadowService>> {//受け取るサービスの指定
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

			if (inputService->IsMouseCaptured()) {
				long dx, dy;
				inputService->GetMouseDelta(dx, dy);
				perCameraService->SetMouseDelta(static_cast<float>(dx), static_cast<float>(dy));

				moveSpeed = std::clamp(moveSpeed + (float)mouseWheelV * MOVE_SPEED_WHEEL_RATE * (std::max)(1.0f, moveSpeed / 20.0f), 0.1f, 200.0f);
			}
		}
		else if (mouseWheelV != 0) {
			perCameraService->Move(perCameraService->GetForward() * moveSpeed * (float)mouseWheelV);
			camera2DService->Zoom((float)mouseWheelV);
		}

		if (perCameraService->IsUpdateBuffer())
		{
			Graphics::CameraParams camParams;
			camParams.view = perCameraService->MakeViewMatrix();
			camParams.position = perCameraService->GetEyePos();
			camParams.nearPlane = perCameraService->GetNearClip();
			camParams.farPlane = perCameraService->GetFarClip();
			camParams.fovY = perCameraService->GetFOV();
			camParams.aspect = perCameraService->GetAspectRatio();

			auto dirLight = lightShadowService->GetDirectionalLight();

			lightShadowService->UpdateCascade(camParams, cascadeSceneAABB);
		}
	}
private:
	float moveSpeed = 1.0f;
	Math::AABB3f cascadeSceneAABB = { {0,-500.0f,0},{5000.0f,500.0f,5000.0f} };
};
