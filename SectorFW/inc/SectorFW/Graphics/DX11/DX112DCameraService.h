/*****************************************************************//**
 * @file   DX113DCameraService.h
 * @brief DirectX11用の3Dカメラサービス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../I2DCameraService.h"
#include "DX11BufferManager.h"
#include "../../Math/Matrix.hpp"

namespace SFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX11用の3Dカメラサービス。カメラの定数バッファを管理し、更新する。
		 */
		class DX112DCameraService : public I2DCameraService {
		public:
			/**
			 * @brief カメラの定数バッファの名前
			 */
			constexpr static inline char BUFFER_NAME[] = "DX112DCamera";
			/**
			 * @brief コンストラクタ
			 * @param bufferMgr DX11BufferManagerのポインタ
			 */
			explicit DX112DCameraService(DX11BufferManager* bufferMgr, const uint32_t width, const uint32_t height)
				: I2DCameraService([&] {
				BufferHandle h;
				bufferMgr->Add(DX11BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr) {

				virtualWidth = (float)width, virtualHeight = (float)height;
			}

					/**
					 * @brief カメラの更新関数
					 * @param deltaTime 前のフレームからの経過時間（秒）
					 */
					void Update(double deltaTime) override {
						frameIdx++;

						if (!isUpdateBuffer) return;

						DX11BufferUpdateDesc cbUpdateDesc;
						{
							auto data = bufferManager->Get(cameraBufferHandle);
							cbUpdateDesc.buffer = data.ref().buffer;
						}

						std::unique_lock lock(sharedMutex);

						center += moveVec * static_cast<float>(deltaTime);
						zoom += moveZoom * static_cast<float>(deltaTime);

						currentSlot = frameIdx % RENDER_BUFFER_COUNT;
						RecomputeMatrices_NoLock(currentSlot);

						cbUpdateDesc.data = &cameraBuffer[currentSlot];
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateBuffer(cbUpdateDesc, currentSlot);

						moveVec = Math::Vec2f{ 0.0f, 0.0f };
						moveZoom = 0.0f;
						isUpdateBuffer = false;
					}

		private:
			DX11BufferManager* bufferManager;
		};
	}
}
