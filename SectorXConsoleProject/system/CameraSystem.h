#pragma once

template<typename Partition>
class CameraSystem : public ITypeSystem<
	CameraSystem<Partition>,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<InputService, Graphics::I3DCameraService>>{//受け取るサービスの指定
	//using Accessor = ComponentAccessor<>;

	static constexpr float MOVE_SPEED_WHEEL_RATE = 0.5f;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<InputService> inputService,
		UndeletablePtr<Graphics::I3DCameraService> cameraService) {
		int mouseWheelV, mouseWheelH;
		inputService->GetMouseWheel(mouseWheelV, mouseWheelH);

		if (inputService->IsRButtonPressed()) {
			if (inputService->IsKeyPressed(Input::Key::E)) {
				cameraService->Move(Math::LFAxes::up() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::Q)) {
				cameraService->Move(Math::LFAxes::down() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::W)) {
				cameraService->Move(cameraService->GetForward() * moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::A)) {
				cameraService->Move(cameraService->GetRight() * -moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::S)) {
				cameraService->Move(cameraService->GetForward() * -moveSpeed);
			}

			if (inputService->IsKeyPressed(Input::Key::D)) {
				cameraService->Move(cameraService->GetRight() * moveSpeed);
			}

			if (inputService->IsMouseCaptured()) {
				long dx, dy;
				inputService->GetMouseDelta(dx, dy);
				cameraService->SetMouseDelta(static_cast<float>(dx), static_cast<float>(dy));

				moveSpeed = std::clamp(moveSpeed + (float)mouseWheelV * MOVE_SPEED_WHEEL_RATE * (std::max)(1.0f, moveSpeed / 20.0f), 0.1f, 200.0f);
			}
		}
		else if (mouseWheelV != 0) {
			cameraService->Move(cameraService->GetForward() * moveSpeed * (float)mouseWheelV);
		}
	}
private:
	float moveSpeed = 1.0f;
};
