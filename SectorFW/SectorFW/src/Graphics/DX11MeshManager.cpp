#include "Graphics/DX11/DX11MeshManager.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11MeshData DX11MeshManager::CreateResource(const DX11MeshCreateDesc& desc)
		{
			if (!desc.sourcePath.empty()) {
				std::scoped_lock lock(cacheMutex);
				auto it = meshCache.find(desc.sourcePath);
				if (it != meshCache.end()) return it->second;
			}

			DX11MeshData mesh{};
			// Vertex Buffer
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.ByteWidth = (UINT)desc.vSize;
			vbDesc.Usage = D3D11_USAGE_DEFAULT;
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = desc.vertices;
			device->CreateBuffer(&vbDesc, &vbData, &mesh.vb);

			// Index Buffer
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)desc.iSize;
			ibDesc.Usage = D3D11_USAGE_DEFAULT;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA ibData = {};
			ibData.pSysMem = desc.indices;
			device->CreateBuffer(&ibDesc, &ibData, &mesh.ib);

			mesh.indexCount = (uint32_t)(desc.iSize / sizeof(uint32_t));
			mesh.stride = (uint32_t)desc.stride;

			if (!desc.sourcePath.empty()) {
				std::scoped_lock lock(cacheMutex);
				auto node = meshCache.emplace(desc.sourcePath, mesh);
				mesh.path = node.first->first;
			}

			return mesh;
		}
		void DX11MeshManager::ScheduleDestroy(uint32_t idx, uint64_t deleteFrame)
		{
			slots[idx].alive = false;
			pendingDelete.push_back({ idx, deleteFrame });
		}
		void DX11MeshManager::ProcessDeferredDeletes(uint64_t currentFrame)
		{
			auto it = pendingDelete.begin();
			while (it != pendingDelete.end()) {
				if (it->deleteSync <= currentFrame) {
					auto& data = slots[it->index].data;

					if (data.vb) { data.vb->Release(); data.vb = nullptr; }
					if (data.ib) { data.ib->Release(); data.ib = nullptr; }

					auto meshIt = meshCache.find(std::wstring(data.path));
					if (meshIt != meshCache.end()) {
						meshCache.erase(meshIt);
					}

					freeList.push_back(it->index);
					it = pendingDelete.erase(it);
				}
				else {
					++it;
				}
			}
		}
	}
}
