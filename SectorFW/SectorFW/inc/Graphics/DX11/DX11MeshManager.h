#pragma once

#include "DX11MaterialManager.h"
#include "DX11TextureManager.h"

#include <string>

namespace SectorFW
{
	namespace Graphics
	{
		struct DX11MeshCreateDesc {
			const void* vertices;
			size_t vSize;
			size_t stride;
			const uint32_t* indices;
			size_t iSize;
			std::wstring sourcePath;
		};

		struct DX11MeshData {
			ComPtr<ID3D11Buffer> vb = nullptr;
			ComPtr<ID3D11Buffer> ib = nullptr;
			uint32_t indexCount = 0;
			uint32_t stride = 0;
		private:
			std::wstring_view path; // キャッシュ用パス

			friend class DX11MeshManager;
		};

		class DX11MeshManager : public ResourceManagerBase<DX11MeshManager, MeshHandle, DX11MeshCreateDesc, DX11MeshData> {
		public:
			explicit DX11MeshManager(ID3D11Device* dev) : device(dev) {}

			DX11MeshData CreateResource(const DX11MeshCreateDesc& desc);

			void ScheduleDestroy(uint32_t idx, uint64_t deleteFrame);

			void ProcessDeferredDeletes(uint64_t currentFrame);
		private:
			struct PendingDelete { uint32_t index; uint64_t deleteSync; };
			std::vector<PendingDelete> pendingDelete;
			ID3D11Device* device;

			std::unordered_map<std::wstring, DX11MeshData> meshCache;
			std::mutex cacheMutex;
		};
	}
}
