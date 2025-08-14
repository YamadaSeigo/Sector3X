#pragma once

#include "RenderTypes.h"
#include "Core/ECS/ServiceContext.hpp"
#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"
#include "Math/convert.hpp"

#include <shared_mutex>

namespace SectorFW
{
	namespace Graphics
	{
		class I3DCameraService : public ECS::IUpdateService {
			friend class ECS::ServiceLocator;
		public:
			explicit I3DCameraService(BufferHandle bufferHandle) noexcept : cameraBufferHandle(bufferHandle) {};
			virtual ~I3DCameraService() = default;

			void Move(const Math::Vec3f& vec) noexcept {
				std::unique_lock lock(sharedMutex);

				moveVec += vec;
				isUpdateBuffer = true;
			}

			void Rotate(const Math::Quatf& rotation) noexcept {
				std::unique_lock lock(sharedMutex);

				Math::Vec3f forward = eye - pos;
				forward = rotation.RotateVector(forward);
				eye = pos + forward;

				isUpdateBuffer = true;
			}

			void SetPosition(const Math::Vec3f& position) noexcept {
				std::unique_lock lock(sharedMutex);

				pos = position;
				isUpdateBuffer = true;
			}

			void SetEye(const Math::Vec3f& eyePosition) noexcept {
				std::unique_lock lock(sharedMutex);

				eye = eyePosition;
				isUpdateBuffer = true;
			}

			void SetUp(const Math::Vec3f& upVector) noexcept {
				std::unique_lock lock(sharedMutex);

				up = upVector;
				isUpdateBuffer = true;
			}

			void SetFOV(float fovRad) noexcept {
				std::unique_lock lock(sharedMutex);

				this->fovRad = fovRad;
				isUpdateBuffer = true;
			}

			void SetAspectRatio(float aspectRatio) noexcept {
				std::unique_lock lock(sharedMutex);

				this->aspectRatio = aspectRatio;
				isUpdateBuffer = true;
			}

			void SetNearClip(float nearClip) noexcept {
				std::unique_lock lock(sharedMutex);

				this->nearClip = nearClip;
				isUpdateBuffer = true;
			}

			void SetFarClip(float farClip) noexcept {
				std::unique_lock lock(sharedMutex);

				this->farClip = farClip;
				isUpdateBuffer = true;
			}

			void SetMouseDelta(float deltaX, float deltaY) noexcept {
				std::unique_lock lock(sharedMutex);
				dx = deltaX;
				dy = deltaY;
				isUpdateBuffer = true;
			}

			void SetMouseSensitivity(float sensX, float sensY) noexcept {
				std::unique_lock lock(sharedMutex);
				sensX_rad_per_px = sensX;
				sensY_rad_per_px = sensY;
			}

			Math::Vec3f GetPosition() const noexcept {
				std::shared_lock lock(sharedMutex);

				return pos;
			}

			Math::Vec3f GetEye() const noexcept {
				std::shared_lock lock(sharedMutex);

				return eye;
			}

			Math::Vec3f GetUp() const noexcept {
				std::shared_lock lock(sharedMutex);

				return up;
			}

			float GetFOV() const noexcept {
				std::shared_lock lock(sharedMutex);

				return fovRad;
			}

			float GetAspectRatio() const noexcept {
				std::shared_lock lock(sharedMutex);

				return aspectRatio;
			}

			float GetNearClip() const noexcept {
				std::shared_lock lock(sharedMutex);

				return nearClip;
			}

			float GetFarClip() const noexcept {
				std::shared_lock lock(sharedMutex);

				return farClip;
			}

			Math::Vec3f GetForward() const noexcept {
				std::shared_lock lock(sharedMutex);

				return (eye - pos).normalized();
			}

			Math::Vec3f GetRight() const noexcept {
				Math::Vec3f forward = GetForward();
				return Math::RFAxes::makeRight(Math::RFAxes::up(), forward);
			}
		private:
			virtual void Update(double deltaTime) override {
				if (!isUpdateBuffer) return; // 更新が必要ない場合は何もしない

				std::unique_lock lock(sharedMutex);

				moveVec = Math::Vec3f{ 0.0f, 0.0f, 0.0f }; // 移動ベクトルをリセット
				UpdateCameraFromMouse(static_cast<float>(deltaTime));
				isUpdateBuffer = false;
			}

		protected:
			void UpdateCameraFromMouse(float dt)
			{
				// マウス → 角度（1pxあたり何ラジアン回すかを sens* で決める）
				float yaw = dx * sensX_rad_per_px * dt; // 右に動かすと右旋回にしたい等で符号調整
				float pitch = dy * sensY_rad_per_px * dt;

				// ピッチ制限（オススメ：累積角で管理）
				float newPitch = std::clamp(pitchAccum + pitch, Math::Deg2Rad(-89.0f), Math::Deg2Rad(89.0f));
				pitch = newPitch - pitchAccum;
				pitchAccum = newPitch;

				// まず Yaw（ワールドUpで回す）
				const Math::Vec3f worldUp{ 0,1,0 };
				Math::Quatf qYaw = Math::Quatf::FromAxisAngle(worldUp, yaw);

				// 次に Pitch（カメラのローカルRight軸で回す）
				// Right軸は現在姿勢から(1,0,0)を回して得る
				Math::Vec3f right = rot.RotateVector(Math::Vec3f{ 1,0,0 });
				Math::Quatf qPitch = Math::Quatf::FromAxisAngle(right, pitch);

				rot = qPitch * qYaw * rot;
				rot.Normalize();
			}
		protected:
			BufferHandle cameraBufferHandle;

			Math::Vec3f pos = { 0.0f, 0.0f, -5.0f };
			Math::Vec3f eye = { 0.0f, 0.0f, 0.0f };
			Math::Vec3f up = { 0.0f, 1.0f, 0.0f };
			float fovRad = Math::Deg2Rad(90.0f); // 垂直FOV
			float aspectRatio = 16.0f / 9.0f; // アスペクト比
			float nearClip = 0.1f; // ニアクリップ
			float farClip = 1000.0f; // ファークリップ

			Math::Vec3f moveVec = { 0.0f, 0.0f, 0.0f }; // 移動ベクトル
			Math::Quatf rot = Math::Quatf::FromEuler(0.0f, 0.0f, 0.0f); // 初期回転
			float pitchAccum = 0.0f; // ピッチの累積角度（制限用）

			float dx = 0.0f, dy = 0.0f;
			float sensX_rad_per_px = std::numbers::pi_v<float> / 10.0f, sensY_rad_per_px = std::numbers::pi_v<float> / 10.0f;

			mutable std::shared_mutex sharedMutex;

			bool isUpdateBuffer = true;
		public:
			STATIC_SERVICE_TAG
		};
	}
}
