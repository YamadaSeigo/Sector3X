#pragma once

#include <wrl/client.h>

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

			void BindSRVsImpl(const std::vector<ID3D11ShaderResourceView*>& srvs, uint32_t startSlot = 0) {
				context->PSSetShaderResources(startSlot, (UINT)srvs.size(), srvs.data());
			}

			void BindCBVsImpl(const std::vector<ID3D11Buffer*>& cbvs, uint32_t startSlot = 0) {
				context->VSSetConstantBuffers(startSlot, (UINT)cbvs.size(), cbvs.data());
			}

			void BindGlobalCBVsImpl(const std::vector<BufferHandle>& cbvs) {
				std::vector<ID3D11Buffer*> buffers;
				buffers.reserve(cbvs.size());
				for (const auto& cb : cbvs) {
					auto d = cbManager->Get(cb);
					buffers.push_back(d.ref().buffer.Get());
				}
				context->VSSetConstantBuffers(0, (UINT)buffers.size(), buffers.data());
			}

			void BeginFrameUploadImpl(const InstanceData* framePool, uint32_t instCount)
			{
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(m_instanceSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				memcpy(m.pData, framePool, instCount * sizeof(InstanceData));
				context->Unmap(m_instanceSB.Get(), 0);

				context->VSSetShaderResources(0, 1, m_instanceSRV.GetAddressOf()); // t0 バインド
			}

			void ExecuteDrawIndexedInstancedImpl(const std::vector<DrawCommand>& cmds, bool usePSORasterizer);

			void ProcessDeferredDeletesImpl(uint64_t currentFrame);

		private:
			void DrawInstanced(uint32_t meshIdx, uint32_t matIdx, uint32_t psoIdx, uint32_t count, bool usePSORasterizer);

			HRESULT CreateInstanceBuffer();
			HRESULT CreateRasterizerStates();
			HRESULT CreateBlendStates();
			HRESULT CreateDepthStencilStates();

			//void UpdateInstanceBuffer(const void* pInstancesData, size_t dataSize);

			void BeginIndexStream()
			{
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(m_instIndexSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				m_idxMapped = static_cast<std::byte*>(m.pData);
				m_idxHead = 0;
			}

			void EndIndexStream()
			{
				context->Unmap(m_instIndexSB.Get(), 0);
				m_idxMapped = nullptr;

				context->VSSetShaderResources(1, 1, m_instIndexSRV.GetAddressOf()); // t0 を VS にセット
			}

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

			// フレーム行列用 StructuredBuffer (SRV)
			ComPtr<ID3D11Buffer>            m_instanceSB;
			ComPtr<ID3D11ShaderResourceView> m_instanceSRV;

			// インデックス列用 StructuredBuffer (SRV)  ※uint（4B/要素）
			ComPtr<ID3D11Buffer>            m_instIndexSB;
			ComPtr<ID3D11ShaderResourceView> m_instIndexSRV;

			// PerDraw 定数バッファ（gIndexBase, gIndexCount）
			ComPtr<ID3D11Buffer>            m_perDrawCB;

			// CPU書き込み用ポインタ（Mapした間だけ有効）
			std::byte* m_idxMapped = nullptr;
			uint32_t    m_idxHead = 0;  // 書き込みカーソル

			ComPtr<ID3D11RasterizerState> rasterizerStates[(size_t)RasterizerStateID::MAX_COUNT];
			ComPtr<ID3D11BlendState> blendStates[(size_t)BlendStateID::MAX_COUNT];
			ComPtr<ID3D11DepthStencilState> depthStencilStates[(size_t)DepthStencilStateID::MAX_COUNT];
		};
	}
}
