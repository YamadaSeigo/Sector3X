#pragma once

#include "dx12inc.h"
#include "../RenderBackend.hpp"
#include "../RenderTypes.h"

namespace SFW
{
	namespace Graphics
	{
		class DX12Backend : public RenderBackendBase<DX12Backend, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> {
		public:
			DX12Backend(ID3D12GraphicsCommandList* cmd) : cmdList(cmd) {}

			void SetRenderTargetsImpl(const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvs, void* dsv) {
				cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), FALSE, (D3D12_CPU_DESCRIPTOR_HANDLE*)dsv);
			}

			void BindSRVsImpl(const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>& srvs) {
				cmdList->SetGraphicsRootDescriptorTable(0, srvs[0]);
			}

			void ExecuteDrawImpl(const DrawCommand& cmd) {
				/*  cmdList->IASetVertexBuffers(0, 1, (D3D12_VERTEX_BUFFER_VIEW*)&cmd.vb);
				  cmdList->IASetIndexBuffer((D3D12_INDEX_BUFFER_VIEW*)&cmd.ib);
				  cmdList->DrawIndexedInstanced(cmd.indexCount, 1, 0, 0, 0);*/
			}

		private:
			ID3D12GraphicsCommandList* cmdList;
		};
	}
}
