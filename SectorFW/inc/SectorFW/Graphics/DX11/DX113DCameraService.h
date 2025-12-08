/*****************************************************************//**
 * @file   DX113DCameraService.h
 * @brief DirectX11用の3Dカメラサービス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../I3DCameraService.h"
#include "DX11BufferManager.h"
#include "../../Math/Matrix.hpp"

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief DirectX11用の3Dカメラサービス。カメラの定数バッファを管理し、更新する。
		 */
		class PerCamera3DService : public I3DCameraService<Perspective> {
		public:
			/**
			 * @brief カメラの定数バッファの名前
			 */
			constexpr static inline char BUFFER_NAME[] = "DX113DPerCamera";
			/**
			 * @brief コンストラクタ
			 * @param bufferMgr BufferManagerのポインタ
			 */
			explicit PerCamera3DService(BufferManager* bufferMgr, const uint32_t width, const uint32_t height)
				: I3DCameraService([&] {
				BufferHandle h;
				bufferMgr->Add(BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr) {
				right = (float)width, bottom = (float)height;
			}

					/**
					 * @brief カメラの更新関数
					 * @param deltaTime 前のフレームからの経過時間（秒）
					 */
					void Update(double deltaTime) override {

						++frameIdx;

						if (!isUpdateBuffer) return;

						currentSlot = frameIdx % RENDER_BUFFER_COUNT;

						auto deltaMove = moveVec * static_cast<float>(deltaTime);

						Math::Vec3f r, u, f;
						{
							std::unique_lock lock(sharedMutex);

							Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);

							if (rotateMode == RotateMode::FPS) {
								eye += deltaMove;
								target = eye + f * focusDist;
							}
							else {
								target += deltaMove;
								eye = target - f * focusDist;
							}

							if (dx != 0 || dy != 0) {
								UpdateCameraFromMouse();   // rot をここで更新
							}
						}

						auto& buffer = cameraBuffer[currentSlot];

						buffer.view = Math::MakeLookAtMatrixLH(eye, target, u);  // 新しい u を使用
						buffer.proj = Math::MakePerspectiveFovT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(fovRad, aspectRatio, nearClip, farClip);

						BufferUpdateDesc cbUpdateDesc;
						{
							auto data = bufferManager->Get(cameraBufferHandle);
							cbUpdateDesc.buffer = data.ref().buffer;
						}

						buffer.viewProj = buffer.proj * buffer.view; // ビュー投影行列
						cbUpdateDesc.data = &buffer;
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateBuffer(cbUpdateDesc, currentSlot);

						moveVec = Math::Vec3f(0.0f, 0.0f, 0.0f); // 移動ベクトルをリセット
						isUpdateBuffer = false;
					}

		private:
			BufferManager* bufferManager;
		};

		/**
		 * @brief DirectX11用の3Dカメラサービス。カメラの定数バッファを管理し、更新する。
		 */
		class OrtCamera3DService : public I3DCameraService<Orthographic> {
		public:
			/**
			 * @brief カメラの定数バッファの名前
			 */
			constexpr static inline char BUFFER_NAME[] = "3DOrtCamera";
			/**
			 * @brief コンストラクタ
			 * @param bufferMgr BufferManagerのポインタ
			 */
			explicit OrtCamera3DService(BufferManager* bufferMgr, const uint32_t width, const uint32_t height)
				: I3DCameraService<Orthographic>([&] {
				BufferHandle h;
				bufferMgr->Add(BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr){
				right = (float)width, bottom = (float)height;
			}

					/**
					 * @brief カメラの更新関数
					 * @param deltaTime 前のフレームからの経過時間（秒）
					 */
					void Update(double deltaTime) override {
						++frameIdx;

						if (!isUpdateBuffer) return;

						auto deltaMove = moveVec * static_cast<float>(deltaTime);

						Math::Vec3f r, u, f;
						{
							std::unique_lock lock(sharedMutex);

							Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);
							if (rotateMode == RotateMode::FPS) {
								eye += deltaMove;
								target = eye + f * focusDist;
							}
							else {
								target += deltaMove;
								eye = target - f * focusDist;
							}

							if (dx != 0 || dy != 0) {
								UpdateCameraFromMouse();   // rot をここで更新
							}
						}

						auto& buffer = cameraBuffer[currentSlot];
						buffer.view = Math::MakeLookAtMatrixLH(eye, target, u);
						buffer.proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(left, right, bottom, top, nearClip, farClip);

						BufferUpdateDesc cbUpdateDesc;
						{
							auto data = bufferManager->Get(cameraBufferHandle);
							cbUpdateDesc.buffer = data.ref().buffer;
						}

						currentSlot = frameIdx % RENDER_BUFFER_COUNT;

						buffer.viewProj = buffer.proj * buffer.view; // ビュー投影行列
						cbUpdateDesc.data = &buffer;
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateBuffer(cbUpdateDesc, currentSlot);

						moveVec = Math::Vec3f(0.0f, 0.0f, 0.0f); // 移動ベクトルをリセット
						isUpdateBuffer = false;
					}

		private:
			BufferManager* bufferManager;
		};
	}
}
