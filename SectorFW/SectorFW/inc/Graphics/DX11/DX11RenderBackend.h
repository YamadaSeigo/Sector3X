#pragma once

#include "../RenderBackend.hpp"

#include "DX11MeshManager.h"
#include "DX11MaterialManager.h"
#include "DX11ShaderManager.h"
#include "DX11PsoManager.h"
#include "DX11TextureManager.h"
#include "DX11ConstantBufferManager.h"
#include "DX11SamplerManager.h"
#include "DX11ModelAssetManager.h"

#include <cassert>

namespace SectorFW
{
	namespace Graphics
	{
		class DX11Backend : public RenderBackendBase<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*> {
		public:
			static inline constexpr uint32_t MAX_INSTANCES = 1024;

			explicit DX11Backend(ID3D11Device* device, ID3D11DeviceContext* context,
				DX11MeshManager* meshMgr, DX11MaterialManager* matMgr,
				DX11ShaderManager* shaderMgr, DX11PSOManager* psoMgr,
				DX11TextureManager* textureMgr,DX11ConstantBufferManager* cbMgr,
				DX11SamplerManager* samplerMgr, DX11ModelAssetManager* modelAssetMgr);

			void AddResourceManagerToRenderServiceImpl(
				RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*>& graph);

			void SetRenderTargetsImpl(const std::vector<ID3D11RenderTargetView*>& rtvs, void* dsv) {
				context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), (ID3D11DepthStencilView*)dsv);
			}

			void BindSRVsImpl(const std::vector<ID3D11ShaderResourceView*>& srvs, UINT startSlot = 0) {
				context->PSSetShaderResources(startSlot, (UINT)srvs.size(), srvs.data());
			}

			void BindCBVsImpl(const std::vector<ID3D11Buffer*>& cbvs, UINT startSlot = 0) {
				context->VSSetConstantBuffers(startSlot, (UINT)cbvs.size(), cbvs.data());
			}

			void ExecuteDrawImpl(const DrawCommand& cmd);

			void ExecuteDrawInstancedImpl(const std::vector<DrawCommand>& cmds);

			void ProcessDeferredDeletesImpl(uint64_t currentFrame);

		private:
			void DrawInstanced(MeshHandle meshHandle, MaterialHandle matHandle, PSOHandle psoHandle,
				const std::vector<InstanceData>& instances);

			void CreateInstanceBuffer();

			void UpdateInstanceBuffer(const std::vector<InstanceData>& instances);

		private:
			ID3D11Device* device;
			ID3D11DeviceContext* context;
			DX11MeshManager* meshManager;
			DX11MaterialManager* materialManager;
			DX11ShaderManager* shaderManager;
			DX11PSOManager* psoManager;
			DX11TextureManager* textureManager;
			DX11ConstantBufferManager* cbManager;
			DX11SamplerManager* samplerManager;
			DX11ModelAssetManager* modelAssetManager;

			Microsoft::WRL::ComPtr<ID3D11Buffer> instanceBuffer;
		};
	}
}
