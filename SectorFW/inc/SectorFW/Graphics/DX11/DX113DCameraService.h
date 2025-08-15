#pragma once

#include "../I3DCameraService.h"
#include "DX11BufferManager.h"
#include "Math/Matrix.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		class DX113DCameraService : public I3DCameraService {
		public:
			constexpr static inline char BUFFER_NAME[] = "DX113DCamera";

			struct CameraBuffer {
				Math::Matrix4x4f viewProj;
			};
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

						std::unique_lock lock(sharedMutex);

						DX11BufferUpdateDesc cbUpdateDesc;
						cbUpdateDesc.handle = cameraBufferHandle;

						auto deltaMove = moveVec * static_cast<float>(deltaTime);
						pos += deltaMove; // カメラ位置を更新
						eye += deltaMove; // 注視点も更新

						if (dx != 0 || dy != 0) {
							// マウスの動きがある場合はカメラを更新
							UpdateCameraFromMouse(static_cast<float>(deltaTime));
							eye = pos + Math::QuatForward<float, Math::LH_ZForward>(rot); // 注視点を更新
						}

						auto view = Math::MakeLookAtMatrixLH(pos, eye, up);
						auto proj = Math::MakePerspectiveMatrixLH(fovRad, aspectRatio, nearClip, farClip);

						static CameraBuffer cameraBuffer;
						cameraBuffer.viewProj = view * proj; // ビュー投影行列
						cbUpdateDesc.data = &cameraBuffer;
						cbUpdateDesc.isDelete = false; // 更新時は削除しない

						cbUpdateDesc.size = sizeof(CameraBuffer);
						bufferManager->UpdateConstantBuffer(cbUpdateDesc);

						moveVec = Math::Vec3f(0.0f, 0.0f, 0.0f); // 移動ベクトルをリセット
						isUpdateBuffer = false;
					}

		private:
			DX11BufferManager* bufferManager;
		};
	}
}
