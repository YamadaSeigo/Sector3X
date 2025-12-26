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
		Graphics::I2DCameraService>
	>
{
	//using Accessor = ComponentAccessor<>;

	static constexpr float MOVE_SPEED_WHEEL_RATE = 0.5f;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(
		safe_ptr<InputService> inputService,
		safe_ptr<Graphics::I3DPerCameraService> perCameraService,
		safe_ptr<Graphics::I2DCameraService> camera2DService) 
	{
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
		else
		{
			if (mouseWheelV != 0) {
				camera2DService->Zoom((float)mouseWheelV);
			}
		}
	}
private:
	float moveSpeed = 1.0f;
};
