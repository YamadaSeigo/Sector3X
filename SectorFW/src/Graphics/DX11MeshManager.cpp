#include "Graphics/DX11/DX11MeshManager.h"

#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11MeshData DX11MeshManager::CreateResource(const DX11MeshCreateDesc& desc, MeshHandle h)
		{
			DX11MeshData mesh{};
			// Vertex Buffer
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.ByteWidth = (UINT)desc.vSize;
			vbDesc.Usage = D3D11_USAGE_DEFAULT;
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			HRESULT hr;

			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = desc.vertices;
			hr = device->CreateBuffer(&vbDesc, &vbData, &mesh.vb);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create vertex buffer: {}", hr);
				assert(false && "Failed to create vertex buffer");
				return {};
			}

			// Index Buffer
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)desc.iSize;
			ibDesc.Usage = D3D11_USAGE_DEFAULT;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA ibData = {};
			ibData.pSysMem = desc.indices;
			hr = device->CreateBuffer(&ibDesc, &ibData, &mesh.ib);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create index buffer: {}", hr);
				assert(false && "Failed to create index buffer");
				return {};
			}

			mesh.indexCount = (uint32_t)(desc.iSize / sizeof(uint32_t));
			mesh.stride = (uint32_t)desc.stride;
			mesh.path = desc.sourcePath;

			return mesh;
		}

		void DX11MeshManager::RemoveFromCaches(uint32_t idx)
		{
			auto& data = slots[idx].data;
			auto pathIt = pathToHandle.find(data.path);
			if (pathIt != pathToHandle.end()) {
				pathToHandle.erase(pathIt);
			}
		}
		void DX11MeshManager::DestroyResource(uint32_t idx, uint64_t currentFrame)
		{
			auto& data = slots[idx].data;
			if (data.vb) { data.vb.Reset(); }
			if (data.ib) { data.ib.Reset(); }
		}
	}
}