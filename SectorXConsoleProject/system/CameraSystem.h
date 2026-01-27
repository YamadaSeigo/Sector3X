#pragma once

#include "graphics/DeferredRenderingService.h"
#include "environment/FireflyService.h"
#include "environment/LeafService.h"

/**
 * @brief カメラのバッファの更新、フルスクリーン描画用のカメラバッファとFireflyServiceのカメラバッファも更新,
 */
template<typename Partition>
class CameraSystem : public ITypeSystem<
	CameraSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<>,
	//受け取るサービスの指定
	ServiceContext<
		InputService,
		Graphics::I3DPerCameraService,
		Graphics::I2DCameraService,
		DeferredRenderingService,
		FireflyService,
		LeafService
#ifdef _DEBUG
		,Graphics::RenderService
#endif
	>>
{
	//using Accessor = ComponentAccessor<>;

	static constexpr float MOVE_SPEED_WHEEL_RATE = 0.5f;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<Graphics::I3DPerCameraService> perCameraService,
		NoDeletePtr<Graphics::I2DCameraService> camera2DService,
		NoDeletePtr<DeferredRenderingService> deferredService,
		NoDeletePtr<FireflyService> fireflyService,
		NoDeletePtr<LeafService> leafService
#ifdef _DEBUG
		, NoDeletePtr<Graphics::RenderService> renderService
#endif
	)
	{
		//※ここのデバック処理はDX11固有なので、perCameraServiceがDX11版であることが前提
#ifdef _DEBUG
		// デバッグのためにカメラ固定
		if (inputService->IsKeyTrigger(Input::Key::F2)) {
			fixedCamera = !fixedCamera;
			if (fixedCamera) {
				debugRot = perCameraService->GetRotation();
				debugEye = perCameraService->GetEyePos();
				debugTarget = perCameraService->GetTarget();
				debugPitchAccum = perCameraService->GetPitchAccum();
			}
			else {
				// 元に戻すために適当な情報をセットしてバッファを更新
				perCameraService->Move({ 0.0f,0.0f,0.0f });
			}
		}

		if (fixedCamera) {

			Math::Vec3f moveVec = { 0.0f,0.0f, 0.0f };

			Math::Vec3f r, u, f;

			Math::ToBasis<float, Math::LH_ZForward>(debugRot, r, u, f);

			if (inputService->IsKeyPressed(Input::Key::E)) {
				moveVec += Math::LFAxes::up() * moveSpeed;
			}

			if (inputService->IsKeyPressed(Input::Key::Q)) {
				moveVec += Math::LFAxes::down() * moveSpeed;
			}

			if (inputService->IsKeyPressed(Input::Key::W)) {
				moveVec += f * moveSpeed;
			}

			if (inputService->IsKeyPressed(Input::Key::A)) {
				moveVec += r * -moveSpeed;
			}

			if (inputService->IsKeyPressed(Input::Key::S)) {
				moveVec += f * -moveSpeed;
			}

			if (inputService->IsKeyPressed(Input::Key::D)) {
				moveVec += r * moveSpeed;
			}


			long dx = 0, dy = 0;
			if (inputService->IsMouseCaptured()) {

				inputService->GetMouseDelta(dx, dy);

				int mouseWheelV, mouseWheelH;
				inputService->GetMouseWheel(mouseWheelV, mouseWheelH);
				moveSpeed = std::clamp(moveSpeed + (float)mouseWheelV * MOVE_SPEED_WHEEL_RATE * (std::max)(1.0f, moveSpeed / 20.0f), 0.1f, 200.0f);
			}

			//適当なdeltaTime
			auto deltaMove = moveVec * static_cast<float>(1.0f / 60.0f);

			debugEye += deltaMove;
			debugTarget = debugEye + f * perCameraService->GetFocusDistance();

			if (dx != 0 || dy != 0) {
				float sensX, sensY;
				perCameraService->GetSensitivity(sensX, sensY);

				// マウス → 角度（1pxあたり何ラジアン回すかを sens* で決める）
				float yaw = dx * sensX; // 右に動かすと右旋回にしたい等で符号調整
				float pitch = dy * sensY;

				// ピッチ制限（オススメ：累積角で管理）
				float newPitch = std::clamp(debugPitchAccum + pitch, Math::Deg2Rad(-89.0f), Math::Deg2Rad(89.0f));
				pitch = newPitch - debugPitchAccum;
				debugPitchAccum = newPitch;

				// 1) Yaw をワールドUpで適用
				const Math::Vec3f worldUp{ 0,1,0 };
				Math::Quatf qYaw = Math::Quatf::FromAxisAngle(worldUp, yaw);
				debugRot = qYaw * debugRot;
				debugRot.Normalize();

				// 2) Yaw 後の Right を取り直して Pitch
				Math::Vec3f right = debugRot.RotateVector(Math::Vec3f{ 1,0,0 });
				Math::Quatf qPitch = Math::Quatf::FromAxisAngle(right, pitch);
				debugRot = qPitch * debugRot;
				debugRot.Normalize();
			}


			auto currentSlot = renderService->GetProduceSlot();

			auto& buffer = debugCameraBuffer[currentSlot];

			buffer.view = Math::MakeLookAtMatrixLH(debugEye, debugTarget, u);  // 新しい u を使用
			float fovRad = perCameraService->GetFOV();
			float aspectRatio = perCameraService->GetAspectRatio();
			float nearClip = perCameraService->GetNearClip();
			float farClip = perCameraService->GetFarClip();
			buffer.proj = Math::MakePerspectiveFovT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(fovRad, aspectRatio, nearClip, farClip);


			auto bufferManager = renderService->GetResourceManager<Graphics::DX11::BufferManager>();

			Graphics::DX11::BufferUpdateDesc cbUpdateDesc;
			{
				auto data = bufferManager->Get(perCameraService->GetCameraBufferHandle());
				cbUpdateDesc.buffer = data.ref().buffer;
			}

			buffer.viewProj = buffer.proj * buffer.view; // ビュー投影行列
			cbUpdateDesc.data = &buffer;
			cbUpdateDesc.isDelete = false; // 更新時は削除しない

			cbUpdateDesc.size = sizeof(Graphics::CameraBuffer);
			bufferManager->UpdateBuffer(cbUpdateDesc, currentSlot);

			Math::ToBasis<float, Math::LH_ZForward>(debugRot, r, u, f);

			//ディファ―ド用のバッファ更新
			LightCameraBuffer lightCameraBufferData{};
			lightCameraBufferData.invViewProj = Math::Inverse(buffer.viewProj);
			lightCameraBufferData.camForward = f.normalized();
			lightCameraBufferData.camPos = debugEye;

			deferredService->UpdateBufferData(lightCameraBufferData);

			FireflyService::CameraCB fireflyCamBuffer{};
			fireflyCamBuffer.gViewProj = buffer.viewProj;
			fireflyCamBuffer.gCamRightWS = r.normalized();
			fireflyCamBuffer.gCamUpWS = u.normalized();

			fireflyService->SetCameraBuffer(fireflyCamBuffer);

			LeafService::CameraCB leafCamBuffer{};
			leafCamBuffer.gViewProj = buffer.viewProj;
			leafCamBuffer.gCamRightWS = r.normalized();
			leafCamBuffer.gCamUpWS = u.normalized();
			leafCamBuffer.gCameraPosWS = debugEye;
			leafCamBuffer.gNearFar = { perCameraService->GetNearClip(), perCameraService->GetFarClip() };

			leafService->SetCameraBuffer(leafCamBuffer);

			return;
		}
#endif

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

				moveSpeed = std::clamp(moveSpeed + (float)mouseWheelV * MOVE_SPEED_WHEEL_RATE * (std::max)(1.0f, moveSpeed / 20.0f), 0.1f, 400.0f);
			}
		}
		else
		{
			if (mouseWheelV != 0) {
				camera2DService->Zoom((float)mouseWheelV);
			}
		}

		const auto& viewProj = perCameraService->GetCameraBufferDataNoLock().viewProj;

		Math::Vec3f r, u, f;
		perCameraService->MakeBasis(r, u, f);

		auto camPos = perCameraService->GetEyePos();

		LightCameraBuffer lightCameraBufferData{};
		lightCameraBufferData.invViewProj = Math::Inverse(viewProj);
		lightCameraBufferData.camForward = f.normalized();
		lightCameraBufferData.camPos = camPos;

		deferredService->UpdateBufferData(lightCameraBufferData);

		auto right = r.normalized();
		auto up = u.normalized();

		FireflyService::CameraCB fireflyCamBuffer{};
		fireflyCamBuffer.gViewProj = viewProj;
		fireflyCamBuffer.gCamRightWS = right;
		fireflyCamBuffer.gCamUpWS = up;

		fireflyService->SetCameraBuffer(fireflyCamBuffer);

		LeafService::CameraCB leafCamBuffer{};
		leafCamBuffer.gViewProj = viewProj;
		leafCamBuffer.gCamRightWS = right;
		leafCamBuffer.gCamUpWS = up;
		leafCamBuffer.gCameraPosWS = camPos;
		leafCamBuffer.gNearFar = { perCameraService->GetNearClip(), perCameraService->GetFarClip() };

		leafService->SetCameraBuffer(leafCamBuffer);
	}
private:
	float moveSpeed = 1.0f;

#ifdef _DEBUG
	//カメラのバッファデータ
	Graphics::CameraBuffer debugCameraBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

	Math::Quatf debugRot;
	Math::Vec3f debugEye;
	Math::Vec3f debugTarget;
	float debugPitchAccum = 0.0f;
	bool fixedCamera = false;
#endif
};
