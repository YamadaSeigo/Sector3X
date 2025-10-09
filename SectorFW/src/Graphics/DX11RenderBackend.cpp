#include "Graphics/DX11/DX11RenderBackend.h"
#include "Graphics/RenderQueue.h"
#include "Debug/logger.h"

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

		void DX11Backend::ProcessDeferredDeletesImpl(uint64_t currentFrame)
		{
			cbManager->PendingUpdates();
			textureManager->PendingUpdates();

			materialManager->ProcessDeferredDeletes(currentFrame);
			meshManager->ProcessDeferredDeletes(currentFrame);
			textureManager->ProcessDeferredDeletes(currentFrame);
			cbManager->ProcessDeferredDeletes(currentFrame);
			samplerManager->ProcessDeferredDeletes(currentFrame);
			modelAssetManager->ProcessDeferredDeletes(currentFrame);
		}

		void DX11Backend::DrawInstanced(uint32_t meshIdx, uint32_t matIdx, uint32_t psoIdx, uint32_t count, bool usePSORasterizer)
		{
			MaterialTemplateID templateID = MaterialTemplateID::MAX_COUNT;
			InputBindingMode bindingMode;
			{
				auto pso = psoManager->GetDirect(psoIdx);

				if (usePSORasterizer) {
					SetRasterizerStateImpl(pso.ref().rasterizerState);
				}

				// PSOバインド
				context->IASetInputLayout(pso.ref().inputLayout.Get());

				auto shader = shaderManager->Get(pso.ref().shader);
				context->VSSetShader(shader.ref().vs.Get(), nullptr, 0);
				context->PSSetShader(shader.ref().ps.Get(), nullptr, 0);

				templateID = shader.ref().templateID;
				bindingMode = shader.ref().bindingMode;
			}
			{
				auto mat = materialManager->GetDirect(matIdx);

				// マテリアルバインド
				if (mat.ref().templateID != templateID) {
					LOG_ERROR("Incompatible Material-Shader: Template mismatch.");
					return;
				}

				// テクスチャSRVバインド
				DX11MaterialManager::BindMaterialPSSRVs(context, mat.ref().psSRV);
				DX11MaterialManager::BindMaterialVSSRVs(context, mat.ref().vsSRV);
				// CBVバインド
				DX11MaterialManager::BindMaterialPSCBVs(context, mat.ref().psCBV);
				DX11MaterialManager::BindMaterialVSCBVs(context, mat.ref().vsCBV);
				// サンプラーバインド
				DX11MaterialManager::BindMaterialSamplers(context, mat.ref().samplerCache);
			}
			{
				auto mesh = meshManager->GetDirect(meshIdx);
				context->IASetIndexBuffer(mesh.ref().ib.Get(), DXGI_FORMAT_R32_UINT, 0);

				switch (bindingMode) {
				case InputBindingMode::AutoStreams:
					BindMeshVertexStreamsForPSO(meshIdx, psoIdx);     // 既存の自動ストリーム
					break;
				case InputBindingMode::OverrideMap:
					BindMeshVertexStreamsFromOverrides(meshIdx, psoIdx); // overrides_/attribMap 準拠でセット（簡易でAutoStreamsと共通でもOK）
					break;
				case InputBindingMode::LegacyManual:
				default: {
					// 旧: 単一AoS VB だけをslot=0に
					ID3D11Buffer* buf = mesh.ref().vbs[0].Get(); // 互換の単一VB
					UINT stride = mesh.ref().stride ? mesh.ref().stride : mesh.ref().strides[0];
					UINT off = 0;
					context->IASetVertexBuffers(0, 1, &buf, &stride, &off);
				} break;
				}

				context->DrawIndexedInstanced(mesh.ref().indexCount, (UINT)count, mesh.ref().startIndex, 0, 0);
			}
		}

		void DX11Backend::BindMeshVertexStreamsForPSO(uint32_t meshIdx, uint32_t psoIdx)
		{
			// PSO の InputLayoutDesc から必要 slot を抽出
			UINT minSlot = UINT_MAX, maxSlot = 0;
			std::bitset<8> needed{};
			{
				auto pso = psoManager->GetDirect(psoIdx);
				auto shader = shaderManager->GetDirect(pso.ref().shader.index);
				for (auto& e : shader.ref().inputLayoutDesc) {
					// インスタンス用は別（今回は slot=4 を想定）
					if (e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA) continue;
					needed.set(e.InputSlot);
					minSlot = (std::min)(minSlot, e.InputSlot);
					maxSlot = (std::max)(maxSlot, e.InputSlot);
				}
			}
			if (minSlot == UINT_MAX) return; // 頂点入力なし（理論上ありえないが）

			// 連続レンジ [minSlot, maxSlot] を IASetVertexBuffers で一括セット
			std::vector<ID3D11Buffer*> bufs;
			std::vector<UINT> strides, offs;
			bufs.reserve(maxSlot - minSlot + 1);
			strides.reserve(bufs.capacity());
			offs.reserve(bufs.capacity());

			{
				auto mesh = meshManager->GetDirect(meshIdx);
				for (UINT s = minSlot; s <= maxSlot; ++s) {
					if (needed.test(s) && mesh.ref().usedSlots.test(s) && mesh.ref().vbs[s]) {
						bufs.push_back(mesh.ref().vbs[s].Get());
						strides.push_back(mesh.ref().strides[s]);
						offs.push_back(mesh.ref().offsets[s]);
					}
					else {
						// gapを埋めるために null を入れる必要はない（StartSlot=minSlot、NumBuffers=bufs.size() で詰めて渡す）
						// ただし InputLayout は slot番号を見ているので、“詰め替え”はできない。
						// → よって gap を作らないように、基本は slot 0..N の設計にしておく。
						// ここでは簡単のため、gap があれば nullptr を入れて維持する。
						bufs.push_back(nullptr);
						strides.push_back(0);
						offs.push_back(0);
					}
				}
			}

			context->IASetVertexBuffers(minSlot, (UINT)bufs.size(), bufs.data(), strides.data(), offs.data());
		}

		void DX11Backend::BindMeshVertexStreamsFromOverrides(uint32_t meshIdx, uint32_t psoIdx)
		{
			// 取得
			auto pso = psoManager->GetDirect(psoIdx);
			auto shader = shaderManager->Get(pso.ref().shader);
			auto mesh = meshManager->GetDirect(meshIdx);

			// この関数は「VS の最終 InputLayout（= 反射＋オーバーライド反映済み）」を信用して
			//  そこに書かれた InputSlot のレンジを連続で IA にセットします。
			UINT minSlot = UINT_MAX, maxSlot = 0;
			for (const auto& elem : shader.ref().inputLayoutDesc) {              // 反射結果は既に保持済み
				if (elem.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA) continue; // インスタンス属性は別経路
				minSlot = (std::min)(minSlot, elem.InputSlot);
				maxSlot = (std::max)(maxSlot, elem.InputSlot);
			}
			if (minSlot == UINT_MAX) {
				// 頂点入力が無い（理論上ほぼ無い）場合は何もしない
				return;
			}

			const UINT num = (maxSlot - minSlot + 1);

			std::vector<ID3D11Buffer*> bufs(num, nullptr);
			std::vector<UINT>          strides(num, 0);
			std::vector<UINT>          offs(num, 0);

			// 必要スロットを埋める（gap は nullptr/0 で埋めて連続レンジ維持）
			// ここで「どのスロットにどの VB を挿すか」は ShaderManager が決めた InputSlot＝最終形に合わせる
			for (const auto& elem : shader.ref().inputLayoutDesc) {
				if (elem.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA) continue;

				const UINT slot = elem.InputSlot;
				const size_t idx = size_t(slot - minSlot);

				// --- SoA（複数VB）パス ---
				// 前提：DX11MeshData に vbs/strides/offsets/usedSlots がある（SoA対応を入れている場合）
				// あるいは AoS しか無いメッシュなら、下のフォールバックを通す
				bool bound = false;
#if 1
				// SoA 対応が入っている場合は、このブロックが生きます
				if (mesh.ref().usedSlots.test(slot) && mesh.ref().vbs[slot]) {
					bufs[idx] = mesh.ref().vbs[slot].Get();
					strides[idx] = mesh.ref().strides[slot];
					offs[idx] = mesh.ref().offsets[slot];
					bound = true;
				}
#endif
				// --- フォールバック：従来 AoS（単一VB） ---
				if (!bound && mesh.ref().vbs[0]) {
					// 既存の単一頂点バッファをそのまま slot に挿す（Layout 側の offset/format は既に決まっている前提）
					bufs[idx] = mesh.ref().vbs[0].Get();
					strides[idx] = mesh.ref().stride;
					offs[idx] = 0;
					bound = true;
				}

				// それでも無ければ nullptr のまま（gap）。この場合、そのセマンティクは未供給なので描画は破綻しうるが、
				// ここで止めずに上位の整合チェックに任せる（ログを出したい場合は WARNING を出す）。
				// LOG_WARNING("Missing vertex stream for slot %u", slot);
			}

			// 連続レンジで一気にセット（gap は nullptr/0 を渡す）
			context->IASetVertexBuffers(minSlot, num, bufs.data(), strides.data(), offs.data());
		}

		HRESULT DX11Backend::CreateInstanceBuffer()
		{
			HRESULT hr;
			auto createStructuredSRV = [&](UINT elemStride, UINT elemCount,
				ID3D11Buffer** pBuf, ID3D11ShaderResourceView** pSRV, D3D11_USAGE usage = D3D11_USAGE_DYNAMIC)
				{
					HRESULT res;
					D3D11_BUFFER_DESC bd{};
					bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					bd.Usage = usage;
					if (usage == D3D11_USAGE_DYNAMIC)
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
			rasterizer.FrontCounterClockwise = TRUE;

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