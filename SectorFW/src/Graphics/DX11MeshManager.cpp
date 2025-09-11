#include "Graphics/DX11/DX11MeshManager.h"

#include "Debug/logger.h"

#include <numbers>

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
			vbDesc.Usage = desc.vUsage;
			if (desc.vUsage == D3D11_USAGE_DYNAMIC) {
				vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			}
			else if (desc.vUsage == D3D11_USAGE_STAGING) {
				vbDesc.CPUAccessFlags = desc.cpuAccessFlags; // CPU アクセスフラグを指定
			}
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			HRESULT hr;

			if (desc.vertices != nullptr) {
				D3D11_SUBRESOURCE_DATA vbData = {};
				vbData.pSysMem = desc.vertices;
				hr = device->CreateBuffer(&vbDesc, &vbData, &mesh.vb);
			}
			else if (vbDesc.Usage == D3D11_USAGE_IMMUTABLE) {
				LOG_ERROR("Immutable vertex buffer must have initial data.");
				assert(false && "Immutable vertex buffer must have initial data.");
				return {};
			}
			else {
				hr = device->CreateBuffer(&vbDesc, nullptr, &mesh.vb);
			}
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create vertex buffer: {}", hr);
				assert(false && "Failed to create vertex buffer");
				return {};
			}

			// Index Buffer
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)desc.iSize;
			ibDesc.Usage = desc.iUsage;
			if (desc.iUsage == D3D11_USAGE_DYNAMIC) {
				ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			}
			else if (desc.iUsage == D3D11_USAGE_STAGING) {
				ibDesc.CPUAccessFlags = desc.cpuAccessFlags; // CPU アクセスフラグを指定
			}
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			if (desc.indices != nullptr) {
				D3D11_SUBRESOURCE_DATA ibData = {};
				ibData.pSysMem = desc.indices;
				hr = device->CreateBuffer(&ibDesc, &ibData, &mesh.ib);
			}
			else if (ibDesc.Usage == D3D11_USAGE_IMMUTABLE) {
				LOG_ERROR("Immutable index buffer must have initial data.");
				assert(false && "Immutable index buffer must have initial data.");
				return {};
			}
			else {
				hr = device->CreateBuffer(&ibDesc, nullptr, &mesh.ib);
			}

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