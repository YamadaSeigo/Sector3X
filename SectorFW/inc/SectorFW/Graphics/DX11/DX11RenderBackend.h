#pragma once

#include "../RenderBackend.hpp"

#include "DX11MeshManager.h"
#include "DX11MaterialManager.h"
#include "DX11ShaderManager.h"
#include "DX11PSOManager.h"
#include "DX11TextureManager.h"
#include "DX11BufferManager.h"
#include "DX11SamplerManager.h"
#include "DX11ModelAssetManager.h"

#include <cassert>

namespace SectorFW
{
	namespace Graphics
	{
		inline D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology t) {
			return D3DTopologyLUT[static_cast<size_t>(t)];
		}

		class DX11Backend : public RenderBackendBase<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*> {
		public:
			static inline constexpr uint32_t MAX_INSTANCES = 1024;

			explicit DX11Backend(ID3D11Device* device, ID3D11DeviceContext* context,
				DX11MeshManager* meshMgr, DX11MaterialManager* matMgr,
				DX11ShaderManager* shaderMgr, DX11PSOManager* psoMgr,
				DX11TextureManager* textureMgr, DX11BufferManager* cbMgr,
				DX11SamplerManager* samplerMgr, DX11ModelAssetManager* modelAssetMgr);

			void AddResourceManagerToRenderServiceImpl(
				RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*>& graph);

			void SetPrimitiveTopologyImpl(PrimitiveTopology topology) {
				context->IASetPrimitiveTopology(ToD3DTopology(topology));
			}

			void SetBlendStateImpl(BlendStateID state);

			void SetRasterizerStateImpl(RasterizerStateID state);

			void SetDepthStencilStateImpl(DepthStencilStateID state) {
				context->OMSetDepthStencilState(depthStencilStates[(size_t)state].Get(), 0);
			}

			void SetRenderTargetsImpl(const std::vector<ID3D11RenderTargetView*>& rtvs, void* dsv) {
				context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), (ID3D11DepthStencilView*)dsv);
			}

			void BindSRVsImpl(const std::vector<ID3D11ShaderResourceView*>& srvs, UINT startSlot = 0) {
				context->PSSetShaderResources(startSlot, (UINT)srvs.size(), srvs.data());
			}

			void BindCBVsImpl(const std::vector<ID3D11Buffer*>& cbvs, UINT startSlot = 0) {
				context->VSSetConstantBuffers(startSlot, (UINT)cbvs.size(), cbvs.data());
			}

			void BindGlobalCBVsImpl(const std::vector<BufferHandle>& cbvs) {
				std::vector<ID3D11Buffer*> buffers;
				buffers.reserve(cbvs.size());
				for (const auto& cb : cbvs) {
					buffers.push_back(cbManager->Get(cb).buffer.Get());
				}
				context->VSSetConstantBuffers(0, (UINT)buffers.size(), buffers.data());
			}

			void ExecuteDrawImpl(const DrawCommand& cmd, bool usePSORastarizer);

			void ExecuteDrawInstancedImpl(const std::vector<DrawCommand>& cmds, bool usePSORasterizer);

			void ProcessDeferredDeletesImpl(uint64_t currentFrame);

		private:
			void DrawInstanced(MeshHandle meshHandle, MaterialHandle matHandle, PSOHandle psoHandle,
				const std::vector<InstanceData>& instances, bool usePSORasterizer);

			HRESULT CreateInstanceBuffer();
			HRESULT CreateRasterizerStates();
			HRESULT CreateBlendStates();
			HRESULT CreateDepthStencilStates();

			void UpdateInstanceBuffer(const std::vector<InstanceData>& instances);

		private:
			ID3D11Device* device;
			ID3D11DeviceContext* context;
			DX11MeshManager* meshManager;
			DX11MaterialManager* materialManager;
			DX11ShaderManager* shaderManager;
			DX11PSOManager* psoManager;
			DX11TextureManager* textureManager;
			DX11BufferManager* cbManager;
			DX11SamplerManager* samplerManager;
			DX11ModelAssetManager* modelAssetManager;

			Microsoft::WRL::ComPtr<ID3D11Buffer> instanceBuffer;

			ComPtr<ID3D11RasterizerState> rasterizerStates[(size_t)RasterizerStateID::MAX_COUNT];
			ComPtr<ID3D11BlendState> blendStates[(size_t)BlendStateID::MAX_COUNT];
			ComPtr<ID3D11DepthStencilState> depthStencilStates[(size_t)DepthStencilStateID::MAX_COUNT];
		};
	}
}
