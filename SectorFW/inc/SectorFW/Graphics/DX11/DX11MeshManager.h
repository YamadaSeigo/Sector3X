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
			std::wstring path; // キャッシュ用パス

			friend class DX11MeshManager;
		};

		class DX11MeshManager : public ResourceManagerBase<DX11MeshManager, MeshHandle, DX11MeshCreateDesc, DX11MeshData> {
		public:
			explicit DX11MeshManager(ID3D11Device* dev) : device(dev) {}

			std::optional<MeshHandle> FindExisting(const DX11MeshCreateDesc& d) {
				if (!d.sourcePath.empty()) {
					if (auto it = pathToHandle.find(d.sourcePath); it != pathToHandle.end())
						return it->second;
				}
				return std::nullopt;
			}
			void RegisterKey(const DX11MeshCreateDesc& d, MeshHandle h) {
				if (!d.sourcePath.empty()) pathToHandle.emplace(d.sourcePath, h);
			}

			DX11MeshData CreateResource(const DX11MeshCreateDesc& desc, MeshHandle h);

			void RemoveFromCaches(uint32_t idx);
			void DestroyResource(uint32_t idx, uint64_t currentFrame);
		private:
			ID3D11Device* device;

			std::unordered_map<std::wstring, MeshHandle> pathToHandle;
		};
	}
}
