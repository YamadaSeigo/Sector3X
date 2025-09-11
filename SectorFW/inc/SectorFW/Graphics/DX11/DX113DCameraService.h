#pragma once

#include "../I3DCameraService.h"
#include "DX11BufferManager.h"
#include "../../Math/Matrix.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		class DX113DCameraService : public I3DCameraService {
		public:
			constexpr static inline char BUFFER_NAME[] = "DX113DCamera";

			/*I3DCameraService(bufferMgr->Add(DX11BufferCreateDesc(BUFFER_NAME, sizeof(CameraBuffer)))),*/

			explicit DX113DCameraService(DX11BufferManager* bufferMgr)
				: I3DCameraService([&] {
				BufferHandle h;
				bufferMgr->Add(DX11BufferCreateDesc{ BUFFER_NAME, sizeof(CameraBuffer) }, h);
				return h;
					}()), bufferManager(bufferMgr) {
			}

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
	}
}
