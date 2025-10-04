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

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX11用の3Dカメラサービス。カメラの定数バッファを管理し、更新する。
		 */
		class DX113DPerCameraService : public I3DCameraService<Perspective> {
		public:
			/**
			 * @brief カメラの定数バッファの名前
			 */
			constexpr static inline char BUFFER_NAME[] = "DX113DPerCamera";
			/**
			 * @brief コンストラクタ
			 * @param bufferMgr DX11BufferManagerのポインタ
			 */
			explicit DX113DPerCameraService(DX11BufferManager* bufferMgr, const uint32_t width, const uint32_t height)
				: I3DCameraService([&] {
				BufferHandle h;
				bufferMgr->Add(DX11BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr) {
				right = (float)width, bottom = (float)height;
			}

					/**
					 * @brief カメラの更新関数
					 * @param deltaTime 前のフレームからの経過時間（秒）
					 */
					void Update(double deltaTime) override {
						if (!isUpdateBuffer) return;

						DX11BufferUpdateDesc cbUpdateDesc;
						{
							auto data = bufferManager->Get(cameraBufferHandle);
							cbUpdateDesc.buffer = data.ref().buffer;
						}

						auto deltaMove = moveVec * static_cast<float>(deltaTime);

						Math::Vec3f r, u, f;
						Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);

						{
							std::unique_lock lock(sharedMutex);
							pos += deltaMove; // カメラ位置を更新
							eye += deltaMove; // 注視点も更新

							if (dx != 0 || dy != 0) {
								// マウスの動きがある場合はカメラを更新
								UpdateCameraFromMouse();
								eye = pos + f * focusDist; // 注視点を更新
							}
						}

						auto view = Math::MakeLookAtMatrixLH(pos, eye, u);
						auto proj = Math::MakePerspectiveMatrixLH(fovRad, aspectRatio, nearClip, farClip);

						cameraBuffer.viewProj = view * proj; // ビュー投影行列
						cbUpdateDesc.data = &cameraBuffer;
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateBuffer(cbUpdateDesc);

						moveVec = Math::Vec3f(0.0f, 0.0f, 0.0f); // 移動ベクトルをリセット
						isUpdateBuffer = false;
					}

		private:
			DX11BufferManager* bufferManager;
		};

		/**
		 * @brief DirectX11用の3Dカメラサービス。カメラの定数バッファを管理し、更新する。
		 */
		class DX113DOrtCameraService : public I3DCameraService<Orthographic> {
		public:
			/**
			 * @brief カメラの定数バッファの名前
			 */
			constexpr static inline char BUFFER_NAME[] = "DX113DOrtCamera";
			/**
			 * @brief コンストラクタ
			 * @param bufferMgr DX11BufferManagerのポインタ
			 */
			explicit DX113DOrtCameraService(DX11BufferManager* bufferMgr, const uint32_t width, const uint32_t height)
				: I3DCameraService<Orthographic>([&] {
				BufferHandle h;
				bufferMgr->Add(DX11BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr){
				right = (float)width, bottom = (float)height;
			}

					/**
					 * @brief カメラの更新関数
					 * @param deltaTime 前のフレームからの経過時間（秒）
					 */
					void Update(double deltaTime) override {
						if (!isUpdateBuffer) return;

						DX11BufferUpdateDesc cbUpdateDesc;
						{
							auto data = bufferManager->Get(cameraBufferHandle);
							cbUpdateDesc.buffer = data.ref().buffer;
						}

						auto deltaMove = moveVec * static_cast<float>(deltaTime);

						Math::Vec3f r, u, f;
						Math::ToBasis<float, Math::LH_ZForward>(rot, r, u, f);

						{
							std::unique_lock lock(sharedMutex);
							pos += deltaMove; // カメラ位置を更新
							eye += deltaMove; // 注視点も更新

							if (dx != 0 || dy != 0) {
								// マウスの動きがある場合はカメラを更新
								UpdateCameraFromMouse();
								eye = pos + f * focusDist; // 注視点を更新
							}
						}

						auto view = Math::MakeLookAtMatrixLH(pos, eye, u);
						auto proj = Math::MakeOrthographicMatrixLH(left, right, bottom, top, nearClip, farClip);

						cameraBuffer.viewProj = view * proj; // ビュー投影行列
						cbUpdateDesc.data = &cameraBuffer;
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateBuffer(cbUpdateDesc);

						moveVec = Math::Vec3f(0.0f, 0.0f, 0.0f); // 移動ベクトルをリセット
						isUpdateBuffer = false;
					}

		private:
			DX11BufferManager* bufferManager;
		};
	}
}
