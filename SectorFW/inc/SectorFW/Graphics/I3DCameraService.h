/*****************************************************************//**
 * @file   I3DCameraService.h
 * @brief 3Dカメラサービスのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "RenderTypes.h"
#include "../Core/ECS/ServiceContext.hpp"
#include "../Math/Vector.hpp"
#include "../Math/Quaternion.hpp"
#include "../Math/convert.hpp"
#include "../Math/Frustum.hpp"

#include <shared_mutex>

namespace SFW
{
	namespace Graphics
	{
		enum ProjectionType : uint8_t{
			Perspective,
			Orthographic
		};

		/**
		* @brief　カメラバッファの構造体
		*/
		struct CameraBuffer {
			Math::Matrix4x4f view;
			Math::Matrix4x4f proj;
			Math::Matrix4x4f viewProj;
		};

		/**
		 * @brief 3Dカメラサービスのインターフェース。カメラの操作、ビュー行列の計算、カメラバッファの管理を行う。
		 */
		template<ProjectionType Type = ProjectionType::Perspective>
		class I3DCameraService : public ECS::IUpdateService {
			friend class ECS::ServiceLocator;
		public:
			enum RotateMode : uint8_t {
				FPS,
				Orbital
			};

			/**
			 * @brief コンストラクタ
			 */
			explicit I3DCameraService(BufferHandle bufferHandle) noexcept : cameraBufferHandle(bufferHandle) {};
			/**
			 * @brief デストラクタ
			 */
			virtual ~I3DCameraService() = default;
			/**
			 * @brief カメラの移動
			 * @param vec 移動ベクトル
			 */
			void Move(const Math::Vec3f& vec) noexcept {
				std::unique_lock lock(sharedMutex);

				moveVec += vec;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラの回転
			 * @param rotation 回転を表すクォータニオン
			 */
			void Rotate(const Math::Quatf& q) noexcept {
				std::unique_lock lock(sharedMutex);
				rot = q * rot;
				rot.Normalize();
				Math::Vec3f r, u, f;
				Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);
				target = eye + f * focusDist;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラの位置を設定
			 * @param position 新しい位置ベクトル
			 */
			void SetEyePos(const Math::Vec3f& eyePos) noexcept {
				std::unique_lock lock(sharedMutex);

				eye = eyePos;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラの注視点を設定
			 * @param eyePosition 新しい注視点ベクトル
			 */
			void SetTarget(const Math::Vec3f& targetPos) noexcept {
				std::unique_lock lock(sharedMutex);

				target = targetPos;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラの視野角を設定
			 * @param fovRad 視野角（ラジアン単位）
			 */
			void SetFOV(float fovRad) noexcept {
				std::unique_lock lock(sharedMutex);

				this->fovRad = fovRad;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラのアスペクト比を設定
			 * @param aspectRatio アスペクト比（幅/高さ）
			 */
			void SetAspectRatio(float aspectRatio) noexcept {
				std::unique_lock lock(sharedMutex);

				this->aspectRatio = aspectRatio;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラのニアクリップ距離を設定
			 * @param nearClip ニアクリップ距離
			 */
			void SetNearClip(float nearClip) noexcept {
				std::unique_lock lock(sharedMutex);

				this->nearClip = nearClip;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラのファークリップ距離を設定
			 * @param farClip ファークリップ距離
			 */
			void SetFarClip(float farClip) noexcept {
				std::unique_lock lock(sharedMutex);

				this->farClip = farClip;
				isUpdateBuffer = true;
			}
			/**
			 * @brief カメラの焦点距離を設定
			 * @param distance 焦点距離
			 */
			void SetFocusDistance(float distance) noexcept {
				std::unique_lock lock(sharedMutex);

				focusDist = (std::max)(1e-6f, distance);

				isUpdateBuffer = true;
			}
			/**
			 * @brief マウスの移動量を設定
			 * @param deltaX X方向の移動量
			 * @param deltaY Y方向の移動量
			 */
			void SetMouseDelta(float deltaX, float deltaY) noexcept {
				std::unique_lock lock(sharedMutex);
				dx = deltaX;
				dy = deltaY;
				isUpdateBuffer = true;
			}
			/**
			 * @brief マウス感度を設定
			 * @param sensX X方向の感度
			 * @param sensY Y方向の感度
			 */
			void SetMouseSensitivity(float sensX, float sensY) noexcept {
				std::unique_lock lock(sharedMutex);
				sensX_rad_per_px = sensX;
				sensY_rad_per_px = sensY;
			}
			/**
			 * @brief カメラの回転モードを設定
			 * @param mode 回転モード（FPSまたはOrbital）
			 */
			void SetRotateMode(RotateMode mode) noexcept {
				std::unique_lock lock(sharedMutex);
				rotateMode = mode;
			}
			/**
			 * @brief カメラの位置を取得
			 * @return Math::Vec3f カメラの位置ベクトル
			 */
			Math::Vec3f GetEyePos() const noexcept {
				std::shared_lock lock(sharedMutex);

				return eye;
			}
			/**
			 * @brief カメラの注視点を取得
			 * @return Math::Vec3f カメラの注視点ベクトル
			 */
			Math::Vec3f GetTarget() const noexcept {
				std::shared_lock lock(sharedMutex);

				return target;
			}
			/**
			 * @brief カメラの上方向ベクトルを取得
			 * @return Math::Vec3f カメラの上方向ベクトル
			 */
			Math::Vec3f GetUp() const noexcept {
				std::shared_lock lock(sharedMutex);

				return Math::QuatUp<float, Math::LH_ZForward>(rot);
			}
			/**
			 * @brief カメラの視野角を取得
			 * @return float 視野角（ラジアン単位）
			 */
			float GetFOV() const noexcept {
				std::shared_lock lock(sharedMutex);

				return fovRad;
			}
			/**
			 * @brief カメラのアスペクト比を取得
			 * @return float アスペクト比（幅/高さ）
			 */
			float GetAspectRatio() const noexcept {
				std::shared_lock lock(sharedMutex);

				return aspectRatio;
			}
			/**
			 * @brief カメラのニアクリップ距離を取得
			 * @return float ニアクリップ距離
			 */
			float GetNearClip() const noexcept {
				std::shared_lock lock(sharedMutex);

				return nearClip;
			}
			/**
			 * @brief カメラのファークリップ距離を取得
			 * @return float ファークリップ距離
			 */
			float GetFarClip() const noexcept {
				std::shared_lock lock(sharedMutex);

				return farClip;
			}
			/**
			 * @brief カメラの焦点距離を取得
			 * @return float 焦点距離
			 */
			Math::Vec3f GetForward() const noexcept {
				std::shared_lock lock(sharedMutex);

				return (target - eye).normalized();
			}
			/**
			 * @brief カメラの右方向ベクトルを取得
			 * @return Math::Vec3f カメラの右方向ベクトル
			 */
			Math::Vec3f GetRight() const noexcept {
				Math::Vec3f forward = GetForward();
				return Math::RFAxes::makeRight(Math::RFAxes::up(), forward);
			}
			/**
			 * @brief カメラの解像度を取得
			 * @return Math::Vec2f カメラの解像度ベクトル (width, height)
			 */
			Math::Vec2f GetResolution() const noexcept {
				std::shared_lock lock(sharedMutex);
				return Math::Vec2f{ right - left, bottom - top };
			}

			float GetFocusDistance() const noexcept {
				std::shared_lock lock(sharedMutex);
				return focusDist;
			}

			Math::Matrix4x4f MakeViewMatrix() const {
				std::shared_lock lock(sharedMutex);
				Math::Vec3f r, u, f;
				Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);
				return Math::MakeLookAtMatrixLH(eye, target, u);
			}

			Math::Matrix4x4f MakeProjectionMatrix() const noexcept {
				std::shared_lock lock(sharedMutex);
				Math::Matrix4x4f proj;
				if constexpr (Type == ProjectionType::Perspective) {
					proj = Math::MakePerspectiveFovT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(fovRad, aspectRatio, nearClip, farClip);
				}
				else {
					proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(left, right, bottom, top, nearClip, farClip);
				}
				return proj;
			}

			Math::Matrix4x4f MakeViewProjMatrix() const {
				std::shared_lock lock(sharedMutex);
				Math::Vec3f r, u, f;
				Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);
				auto view = Math::MakeLookAtMatrixLH(eye, target, u);
				Math::Matrix4x4f proj;
				if constexpr (Type == ProjectionType::Perspective) {
					proj = Math::MakePerspectiveFovT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(fovRad, aspectRatio, nearClip, farClip);
				}
				else {
					proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(left, right, bottom, top, nearClip, farClip);
				}
				return proj * view;
			}

			Math::Matrix4x4f MakeViewProjMatrix(float _nearClip, float _farClip) const {
				std::shared_lock lock(sharedMutex);
				Math::Vec3f r, u, f;
				Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);
				auto view = Math::MakeLookAtMatrixLH(eye, target, u);
				Math::Matrix4x4f proj;
				if constexpr (Type == ProjectionType::Perspective) {
					proj = Math::MakePerspectiveFovT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(fovRad, aspectRatio, _nearClip, _farClip);
				}
				else {
					proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(left, right, bottom, top, _nearClip, _farClip);
				}
				return proj * view;
			}

			void MakeBasis(Math::Vec3f& outRight, Math::Vec3f& outUp, Math::Vec3f& outForward) const noexcept {
				std::shared_lock lock(sharedMutex);
				Math::ToBasis<float, Math::LH_ZForward>(rot, outRight, outUp, outForward);
			}

			/**
			 * @brief ビュー行列を取得
			 * @return Math::Matrix4x4f ビュー行列
			 */
			Math::Frustumf MakeFrustum(bool normalize = false) const {
				auto fru = Math::Frustumf::FromRowMajor(cameraBuffer[currentSlot].viewProj.data(), normalize);
				Math::Frustumf::FaceInward(fru, eye, GetForward(), nearClip);

				return fru;
			}
			/**
			 * @brief カメラバッファのデータを取得
			 * @return const CameraBuffer& カメラバッファのデータ
			 */
			CameraBuffer GetCameraBufferData() const noexcept {
				return cameraBuffer[currentSlot];
			}
			/**
			 * @brief 遅延分前のカメラバッファのデータを取得
			 * @return const CameraBuffer& カメラバッファのデータ
			 */
			CameraBuffer GetOldCameraBufferData() const noexcept {
				return cameraBuffer[(currentSlot + (RENDER_BUFFER_COUNT - 1)) % RENDER_BUFFER_COUNT];
			}

			/**
			 * @brief カメラバッファの更新が必要かどうかを取得
			 * @return bool 更新が必要な場合はtrue、そうでなければfalse
			 */
			bool IsUpdateBuffer() const noexcept {
				std::shared_lock lock(sharedMutex);
				return isUpdateBuffer;
			}
		private:
			virtual void Update(double deltaTime) override {
				++frameIdx;

				if (!isUpdateBuffer) return; // 更新が必要ない場合は何もしない

				currentSlot = frameIdx % RENDER_BUFFER_COUNT;

				std::unique_lock lock(sharedMutex);

				moveVec = Math::Vec3f{ 0.0f, 0.0f, 0.0f }; // 移動ベクトルをリセット
				UpdateCameraFromMouse();
				isUpdateBuffer = false;
			}

		protected:
			/**
			 * @brief マウスの移動に基づいてカメラの回転を更新する
			 */
			void UpdateCameraFromMouse()
			{
				// マウス → 角度（1pxあたり何ラジアン回すかを sens* で決める）
				float yaw = dx * sensX_rad_per_px; // 右に動かすと右旋回にしたい等で符号調整
				float pitch = dy * sensY_rad_per_px;

				// ピッチ制限（オススメ：累積角で管理）
				float newPitch = std::clamp(pitchAccum + pitch, Math::Deg2Rad(-89.0f), Math::Deg2Rad(89.0f));
				pitch = newPitch - pitchAccum;
				pitchAccum = newPitch;

				// 1) Yaw をワールドUpで適用
				const Math::Vec3f worldUp{ 0,1,0 };
				Math::Quatf qYaw = Math::Quatf::FromAxisAngle(worldUp, yaw);
				rot = qYaw * rot;
				rot.Normalize();

				// 2) Yaw 後の Right を取り直して Pitch
				Math::Vec3f right = rot.RotateVector(Math::Vec3f{ 1,0,0 });
				Math::Quatf qPitch = Math::Quatf::FromAxisAngle(right, pitch);
				rot = qPitch * rot;
				rot.Normalize();
			}
		protected:
			// カメラバッファハンドルとデータ
			BufferHandle cameraBufferHandle;
			// フレームインデックス
			uint64_t frameIdx = 0; //
			//カメラのバッファデータ
			CameraBuffer cameraBuffer[RENDER_BUFFER_COUNT] = {};
			// カメラの位置
			Math::Vec3f eye = { 0.0f, 0.0f, -5.0f };
			// カメラの注視点
			Math::Vec3f target = { 0.0f, 0.0f, 0.0f };
			// 垂直FOV
			float fovRad = Math::Deg2Rad(90.0f);
			// アスペクト比
			float aspectRatio = 16.0f / 9.0f;
			// ニアクリップ
			float nearClip = 0.1f;
			// ファークリップ
			float farClip = 1000.0f;

			float left = 0.0;
#ifdef SCREEN_WIDTH
			float right = SCREEN_WIDTH;
#else
			float right = 1080.0f;
#endif
#ifdef SCREEN_HEIGHT
			float bottom = SCREEN_HEIGHT;
#else
			float bottom = 1920.0f;
#endif
			float top = 0.0f;

			// 注視点までの距離
			float focusDist = 10.0f;
			// 移動ベクトル
			Math::Vec3f moveVec = { 0.0f, 0.0f, 0.0f };
			// ピッチの累積角度（制限用）
			float pitchAccum = 0.0f;
			// 現在のスロット
			uint16_t currentSlot = 0;
			// 更新フラグ
			bool isUpdateBuffer = true;
			// 初期回転
			Math::Quatf rot = Math::Quatf::FromEuler(0.0f, 0.0f, 0.0f);
			// マウス移動量
			float dx = 0.0f, dy = 0.0f;
			// マウス感度（ラジアン/ピクセル）
			float sensX_rad_per_px = std::numbers::pi_v<float> / 600.0f, sensY_rad_per_px = std::numbers::pi_v<float> / 600.0f;
			// スレッドセーフ用の共有ミューテックス
			mutable std::shared_mutex sharedMutex;

			// 回転モード
			RotateMode rotateMode = RotateMode::Orbital;
		public:
			STATIC_SERVICE_TAG
		};

		using I3DPerCameraService = I3DCameraService<Perspective>;
		using I3DOrtCameraService = I3DCameraService<Orthographic>;
	}
}
