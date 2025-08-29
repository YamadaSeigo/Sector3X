#include "Graphics/DX11/DX11RenderBackend.h"
#include "Graphics/RenderQueue.hpp"
#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11Backend::DX11Backend(ID3D11Device* device, ID3D11DeviceContext* context,
			DX11MeshManager* meshMgr, DX11MaterialManager* matMgr,
			DX11ShaderManager* shaderMgr, DX11PSOManager* psoMgr,
			DX11TextureManager* textureMgr, DX11BufferManager* cbMgr,
			DX11SamplerManager* samplerMgr, DX11ModelAssetManager* modelAssetMgr)
			: device(device), context(context),
			meshManager(meshMgr), materialManager(matMgr),
			shaderManager(shaderMgr), psoManager(psoMgr),
			textureManager(textureMgr), cbManager(cbMgr),
			samplerManager(samplerMgr), modelAssetManager(modelAssetMgr) {
			HRESULT hr;
			hr = CreateInstanceBuffer();
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create instance buffer for DX11Backend.");
				assert(false && "Failed to create instance buffer");
			}
			hr = CreateRasterizerStates();
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create rasterizer states for DX11Backend.");
				assert(false && "Failed to create rasterizer states");
			}
			hr = CreateBlendStates();
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create blend states for DX11Backend.");
				assert(false && "Failed to create blend states");
			}
			hr = CreateDepthStencilStates();
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil states for DX11Backend.");
				assert(false && "Failed to create depth stencil states");
			}

			assert(device && context && meshManager && materialManager && shaderManager && psoManager &&
				textureManager && cbManager && samplerManager &&
				"DX11Backend requires valid device, context, and managers.");
		}

		void DX11Backend::AddResourceManagerToRenderServiceImpl(RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*>& graph)
		{
			graph.RegisterResourceManager<DX11MeshManager>(meshManager);
			graph.RegisterResourceManager<DX11MaterialManager>(materialManager);
			graph.RegisterResourceManager<DX11ShaderManager>(shaderManager);
			graph.RegisterResourceManager<DX11PSOManager>(psoManager);
			graph.RegisterResourceManager<DX11TextureManager>(textureManager);
			graph.RegisterResourceManager<DX11BufferManager>(cbManager);
			graph.RegisterResourceManager<DX11SamplerManager>(samplerManager);
			graph.RegisterResourceManager<DX11ModelAssetManager>(modelAssetManager);
		}

		void DX11Backend::SetBlendStateImpl(BlendStateID state)
		{
			if (state < BlendStateID(0) || state >= BlendStateID::MAX_COUNT) {
				LOG_ERROR("Invalid BlendStateID: %d", static_cast<int>(state));
				assert(false && "Invalid BlendStateID");
				return;
			}

			auto& blendState = blendStates[static_cast<size_t>(state)];
			if (blendState) {
				context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
			}
			else {
				// デフォルトのブレンドステートを設定
				context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
				LOG_WARNING("BlendStateID %d is not set, using default blend state.", static_cast<int>(state));
			}
		}

		void DX11Backend::SetRasterizerStateImpl(RasterizerStateID state)
		{
			if (state < RasterizerStateID(0) || state >= RasterizerStateID::MAX_COUNT) {
				LOG_ERROR("Invalid RasterizerStateID: %d", static_cast<int>(state));
				assert(false && "Invalid RasterizerStateID");
				return;
			}

			auto& rasterizerState = rasterizerStates[static_cast<size_t>(state)];
			if (rasterizerState) {
				context->RSSetState(rasterizerState.Get());
			}
			else {
				// デフォルトのラスタライザーステートを設定
				context->RSSetState(nullptr);
			}
		}

		void DX11Backend::ExecuteDrawImpl(const DrawCommand& cmd, bool usePSORasterizer)
		{
			const auto& mesh = meshManager->GetDirect(cmd.mesh);
			const auto& mat = materialManager->GetDirect(cmd.material);
			const auto& pso = psoManager->GetDirect(cmd.pso);
			const auto& shader = shaderManager->Get(pso.shader);

			if (usePSORasterizer) {
				SetRasterizerStateImpl(pso.rasterizerState);
			}

			context->IASetInputLayout(pso.inputLayout.Get());
			context->VSSetShader(shader.vs.Get(), nullptr, 0);
			context->PSSetShader(shader.ps.Get(), nullptr, 0);

			// SRVバインド
			DX11MaterialManager::BindMaterialPSSRVs(context, mat.psSRV);
			DX11MaterialManager::BindMaterialVSSRVs(context, mat.vsSRV);
			// CBVバインド
			DX11MaterialManager::BindMaterialPSCBVs(context, mat.psCBV);
			DX11MaterialManager::BindMaterialVSCBVs(context, mat.vsCBV);
			// サンプラーバインド
			DX11MaterialManager::BindMaterialSamplers(context, mat.samplerCache);

			UINT stride = mesh.stride;
			UINT offset = 0;
			context->IASetVertexBuffers(0, 1, mesh.vb.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0);

			context->DrawIndexed(mesh.indexCount, 0, 0);
		}

		void DX11Backend::ExecuteDrawInstancedImpl(const std::vector<DrawCommand>& cmds, bool usePSORasterizer)
		{
			struct DrawBatch {
				uint32_t mesh;
				uint32_t material;
				uint32_t pso;
				uint32_t base;
				uint32_t instanceCount; // instances[idx]
			};

			BeginIndexStream();

			size_t i = 0;
			size_t cmdCount = cmds.size();
			std::vector<DrawBatch> batches;
			while (i < cmdCount) {
				auto currentPSO = cmds[i].pso;
				auto currentMat = cmds[i].material;

				uint32_t currentMesh = cmds[i].mesh;
				uint32_t instanceCount = 0;

				const uint32_t base = m_idxHead;        // このドローの index 先頭

				// 1) 同PSO/Mat/Mesh を束ねつつ、index を SRV に“直接”書く
				auto* dst = reinterpret_cast<uint32_t*>(m_idxMapped) + m_idxHead;

				// 同じPSO + Material + Meshをまとめる
				while (i < cmdCount &&
					cmds[i].pso == currentPSO &&
					cmds[i].material == currentMat &&
					cmds[i].mesh == currentMesh &&
					instanceCount < MAX_INSTANCES) {
					dst[instanceCount++] = cmds[i].instanceIndex.index; // ← 直接書く = instances[idx];
					++i;
				}
				m_idxHead += instanceCount;

				batches.emplace_back(currentMesh, currentMat, currentPSO, base, instanceCount);
			}

			EndIndexStream();

			for (const auto& b : batches) {
				// PerDraw CB に base と count を設定
				struct { uint32_t base, count, pad0, pad1; } perDraw{ b.base, b.instanceCount, 0, 0 };
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(m_perDrawCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				memcpy(m.pData, &perDraw, sizeof(perDraw));
				context->Unmap(m_perDrawCB.Get(), 0);
				context->VSSetConstantBuffers(1, 1, m_perDrawCB.GetAddressOf()); // b1

				DrawInstanced(b.mesh, b.material, b.pso, b.instanceCount, usePSORasterizer);
			}
		}

		void DX11Backend::ProcessDeferredDeletesImpl(uint64_t currentFrame)
		{
			cbManager->PendingUpdates();

			materialManager->ProcessDeferredDeletes(currentFrame);
			meshManager->ProcessDeferredDeletes(currentFrame);
			textureManager->ProcessDeferredDeletes(currentFrame);
			cbManager->ProcessDeferredDeletes(currentFrame);
			samplerManager->ProcessDeferredDeletes(currentFrame);
			modelAssetManager->ProcessDeferredDeletes(currentFrame);
		}

		void DX11Backend::DrawInstanced(uint32_t meshIdx, uint32_t matIdx, uint32_t psoIdx, uint32_t count, bool usePSORasterizer)
		{
			const auto& mesh = meshManager->GetDirect(meshIdx);
			const auto& mat = materialManager->GetDirect(matIdx);
			const auto& pso = psoManager->GetDirect(psoIdx);
			const auto& shader = shaderManager->Get(pso.shader);

			if (usePSORasterizer) {
				SetRasterizerStateImpl(pso.rasterizerState);
			}

			// PSOバインド
			context->IASetInputLayout(pso.inputLayout.Get());
			context->VSSetShader(shader.vs.Get(), nullptr, 0);
			context->PSSetShader(shader.ps.Get(), nullptr, 0);

			// マテリアルバインド
			if (mat.templateID != shader.templateID) {
				LOG_ERROR("Incompatible Material-Shader: Template mismatch.");
				return;
			}

			// テクスチャSRVバインド
			DX11MaterialManager::BindMaterialPSSRVs(context, mat.psSRV);
			DX11MaterialManager::BindMaterialVSSRVs(context, mat.vsSRV);
			// CBVバインド
			DX11MaterialManager::BindMaterialPSCBVs(context, mat.psCBV);
			DX11MaterialManager::BindMaterialVSCBVs(context, mat.vsCBV);
			// サンプラーバインド
			DX11MaterialManager::BindMaterialSamplers(context, mat.samplerCache);

			// インスタンスデータ更新
			//UpdateInstanceBuffer(pInstancesData, (size_t)dataSize);

			UINT strides[1] = { mesh.stride };
			UINT offsets[1] = { 0 };
			ID3D11Buffer* buffers[1] = { mesh.vb.Get() };
			context->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			context->IASetVertexBuffers(0, 1, buffers, strides, offsets);

			context->DrawIndexedInstanced(mesh.indexCount, (UINT)count, 0, 0, 0);
		}

		HRESULT DX11Backend::CreateInstanceBuffer()
		{
			HRESULT hr;
			auto createStructuredSRV = [&](UINT elemStride, UINT elemCount,
				ID3D11Buffer** pBuf, ID3D11ShaderResourceView** pSRV)
				{
					HRESULT res;
					D3D11_BUFFER_DESC bd{};
					bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					bd.Usage = D3D11_USAGE_DYNAMIC;
					bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
					bd.ByteWidth = elemStride * elemCount;
					bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
					bd.StructureByteStride = elemStride;
					res = device->CreateBuffer(&bd, nullptr, pBuf);
					if (FAILED(res)) {
						LOG_ERROR("Failed to create structured buffer for instance data: %d", res);
						return res;
					}

					D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
					sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
					sd.Format = DXGI_FORMAT_UNKNOWN;  // structured
					sd.Buffer.ElementOffset = 0;
					sd.Buffer.ElementWidth = elemCount;
					res = device->CreateShaderResourceView(*pBuf, &sd, pSRV);
					if (FAILED(res)) {
						LOG_ERROR("Failed to create SRV for instance data buffer: %d", res);
						(*pBuf)->Release();
						*pBuf = nullptr;
						return res;
					}

					return S_OK;
				};

			// 例：最大 65k インスタンス／フレーム、最大 1M インデックス／パス
			hr = createStructuredSRV(sizeof(InstanceData), MAX_INSTANCES_PER_FRAME, &m_instanceSB, &m_instanceSRV);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create structured SRV for instance data: %d", hr);
				return hr;
			}

			hr = createStructuredSRV(sizeof(uint32_t), MAX_INDICES_PER_PASS, &m_instIndexSB, &m_instIndexSRV);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create structured SRV for instance index data: %d", hr);
				return hr;
			}

			// PerDraw CB（16B アライン）
			D3D11_BUFFER_DESC cbd{};
			cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbd.Usage = D3D11_USAGE_DYNAMIC;
			cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			cbd.ByteWidth = 16;
			hr = device->CreateBuffer(&cbd, nullptr, &m_perDrawCB);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create per-draw constant buffer: %d", hr);
				return hr;
			}

			return S_OK;
		}

		HRESULT DX11Backend::CreateRasterizerStates()
		{
			//--- カリング設定
			D3D11_RASTERIZER_DESC rasterizer = {};

			D3D11_FILL_MODE fill[] = {
				D3D11_FILL_SOLID,
				D3D11_FILL_WIREFRAME,
			};
			D3D11_CULL_MODE cull[] = {
				D3D11_CULL_BACK,
				D3D11_CULL_FRONT,
				D3D11_CULL_NONE,
			};
			rasterizer.FrontCounterClockwise = true;

			HRESULT hr;
			for (int i = 0; i < 2; ++i) {
				for (int j = 0; j < 3; ++j)
				{
					rasterizer.FillMode = fill[i];
					rasterizer.CullMode = cull[j];
					hr = device->CreateRasterizerState(&rasterizer, rasterizerStates[i * 3 + j].GetAddressOf());
					if (FAILED(hr)) { return hr; }
				}
			}

			SetRasterizerStateImpl(RasterizerStateID::SolidCullBack); // デフォルト

			return S_OK;
		}

		HRESULT DX11Backend::CreateBlendStates()
		{
			//--- アルファブレンディング
			// https://pgming-ctrl.com/directx11/blend/
			D3D11_BLEND_DESC blendDesc = {};
			blendDesc.AlphaToCoverageEnable = FALSE;
			blendDesc.IndependentBlendEnable = FALSE;
			blendDesc.RenderTarget[0].BlendEnable = TRUE;
			blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			D3D11_BLEND blend[(size_t)BlendStateID::MAX_COUNT][2] = {
				{D3D11_BLEND_ONE, D3D11_BLEND_ZERO},
				{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA},
				{D3D11_BLEND_ONE, D3D11_BLEND_ONE},
				{D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_COLOR},
			};

			HRESULT hr;
			for (size_t i = 0; i < (size_t)BlendStateID::MAX_COUNT; ++i)
			{
				blendDesc.RenderTarget[0].SrcBlend = blend[i][0];
				blendDesc.RenderTarget[0].DestBlend = blend[i][1];
				hr = device->CreateBlendState(&blendDesc, blendStates[i].GetAddressOf());
				if (FAILED(hr)) { return hr; }
			}

			return S_OK;
		}

		HRESULT DX11Backend::CreateDepthStencilStates()
		{
			//--- 深度テスト
			// https://tositeru.github.io/ImasaraDX11/part/ZBuffer-and-depth-stencil
			auto create = [&](size_t idx, const D3D11_DEPTH_STENCIL_DESC& desc) -> HRESULT {
				depthStencilStates[idx].Reset(); // 安全のため破棄
				return device->CreateDepthStencilState(&desc, depthStencilStates[idx].GetAddressOf());
				};

			HRESULT hr = S_OK;

			// 0) ベースとなるデフォルト設定
			D3D11_DEPTH_STENCIL_DESC ds = {};
			ds.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			ds.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			ds.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			ds.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			ds.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			ds.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			ds.BackFace = ds.FrontFace;

			// 1) Default: 深度テストON, 書き込みON, LessEqual（不透明/大半の3D）
			{
				auto d = ds;
				d.DepthEnable = TRUE;
				d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
				d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
				d.StencilEnable = FALSE; // 既定はStencil未使用
				hr = create((size_t)DepthStencilStateID::Default, d); if (FAILED(hr)) return hr;
			}

			// 2) DepthReadOnly: 深度テストON, 書き込みOFF（スカイボックス/アルファブレンド/ポスト系）
			{
				auto d = ds;
				d.DepthEnable = TRUE;
				d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
				d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
				d.StencilEnable = FALSE;
				hr = create((size_t)DepthStencilStateID::DepthReadOnly, d); if (FAILED(hr)) return hr;
			}

			// 3) NoDepth: 深度テストOFF, 書き込みOFF（HUD/デバッグオーバーレイ）
			{
				auto d = ds;
				d.DepthEnable = FALSE;
				d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
				d.DepthFunc = D3D11_COMPARISON_ALWAYS;
				d.StencilEnable = FALSE;
				hr = create((size_t)DepthStencilStateID::NoDepth, d); if (FAILED(hr)) return hr;
			}

			return S_OK;
		}

		/*void DX11Backend::UpdateInstanceBuffer(const void* pInstancesData, size_t dataSize)
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			context->Map(instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, pInstancesData, dataSize * sizeof(InstanceData));
			context->Unmap(instanceBuffer.Get(), 0);
		}*/
	}
}