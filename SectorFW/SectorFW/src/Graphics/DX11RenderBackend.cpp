#include "Graphics/DX11/DX11RenderBackend.h"
#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11Backend::DX11Backend(ID3D11Device* device, ID3D11DeviceContext* context,
			DX11MeshManager* meshMgr, DX11MaterialManager* matMgr,
			DX11ShaderManager* shaderMgr, DX11PSOManager* psoMgr,
			DX11TextureManager* textureMgr, DX11ConstantBufferManager* cbMgr,
			DX11SamplerManager* samplerMgr, DX11ModelAssetManager* modelAssetMgr)
			: device(device), context(context),
			meshManager(meshMgr), materialManager(matMgr),
			shaderManager(shaderMgr), psoManager(psoMgr),
			textureManager(textureMgr), cbManager(cbMgr),
			samplerManager(samplerMgr) ,modelAssetManager(modelAssetMgr) {
			CreateInstanceBuffer();
			assert(device && context && meshManager && materialManager && shaderManager && psoManager &&
				textureManager && cbManager && samplerManager && 
				"DX11Backend requires valid device, context, and managers.");
		}

		void DX11Backend::AddResourceManagerToRenderServiceImpl(RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*>& graph)
		{
			graph.RegisterResourceManager<DX11MeshManager>(meshManager);
			graph.RegisterResourceManager<DX11MaterialManager>(materialManager);
			graph.RegisterResourceManager<DX11ShaderManager>(shaderManager);
			graph.RegisterResourceManager<DX11PSOManager>(psoManager);
			graph.RegisterResourceManager<DX11TextureManager>(textureManager);
			graph.RegisterResourceManager<DX11ConstantBufferManager>(cbManager);
			graph.RegisterResourceManager<DX11SamplerManager>(samplerManager);
			graph.RegisterResourceManager<DX11ModelAssetManager>(modelAssetManager);
		}

		void DX11Backend::ExecuteDrawImpl(const DrawCommand& cmd)
		{
			const auto& mesh = meshManager->Get(cmd.mesh);
			const auto& mat = materialManager->Get(cmd.material);
			const auto& pso = psoManager->Get(cmd.pso);
			const auto& shader = shaderManager->Get(pso.shader);

			context->IASetInputLayout(pso.inputLayout.Get());
			context->VSSetShader(shader.vs.Get(), nullptr, 0);
			context->PSSetShader(shader.ps.Get(), nullptr, 0);

			DX11MaterialManager::BindMaterialSRVs(context, mat.textureCache);

			UINT stride = mesh.stride;
			UINT offset = 0;
			context->IASetVertexBuffers(0, 1, mesh.vb.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0);

			context->DrawIndexed(mesh.indexCount, 0, 0);
		}

		void DX11Backend::ExecuteDrawInstancedImpl(const std::vector<DrawCommand>& cmds)
		{
			size_t i = 0;
			size_t cmdCount = cmds.size();
			while (i < cmdCount) {
				auto currentPSO = cmds[i].pso;
				auto currentMat = cmds[i].material;

				std::vector<InstanceData> instanceData;
				MeshHandle currentMesh = cmds[i].mesh;
				uint32_t instanceCount = 0;

				// 同じPSO + Material + Meshをまとめる
				while (i < cmdCount &&
					cmds[i].pso.index == currentPSO.index &&
					cmds[i].material.index == currentMat.index &&
					cmds[i].mesh.index == currentMesh.index && 
					instanceCount < MAX_INSTANCES) {
					instanceData.push_back(cmds[i].instance);
					++i;
					++instanceCount;
				}

				DrawInstanced(currentMesh, currentMat, currentPSO, instanceData);
			}
		}

		void DX11Backend::ProcessDeferredDeletesImpl(uint64_t currentFrame)
		{
			materialManager->ProcessDeferredDeletes(currentFrame);
			meshManager->ProcessDeferredDeletes(currentFrame);
			textureManager->ProcessDeferredDeletes(currentFrame);
			cbManager->ProcessDeferredDeletes(currentFrame);
			samplerManager->ProcessDeferredDeletes(currentFrame);
			modelAssetManager->ProcessDeferredDeletes(currentFrame);
		}

		void DX11Backend::DrawInstanced(MeshHandle meshHandle, MaterialHandle matHandle, PSOHandle psoHandle, 
			const std::vector<InstanceData>& instances)
		{
			const auto& mesh = meshManager->Get(meshHandle);
			const auto& mat = materialManager->Get(matHandle);
			const auto& pso = psoManager->Get(psoHandle);
			const auto& shader = shaderManager->Get(pso.shader);

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
			DX11MaterialManager::BindMaterialSRVs(context, mat.textureCache);
			// CBVバインド
			DX11MaterialManager::BindMaterialCBVs(context, mat.cbvCache);
			// サンプラーバインド
			DX11MaterialManager::BindMaterialSamplers(context, mat.samplerCache);

			// インスタンスデータ更新
			UpdateInstanceBuffer(instances);

			UINT strides[2] = { mesh.stride, sizeof(InstanceData) };
			UINT offsets[2] = { 0, 0 };
			ID3D11Buffer* buffers[2] = { mesh.vb.Get(), instanceBuffer.Get()};
			context->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			context->IASetVertexBuffers(0, 2, buffers, strides, offsets);

			context->DrawIndexedInstanced(mesh.indexCount, (UINT)instances.size(), 0, 0, 0);
		}

		void DX11Backend::CreateInstanceBuffer()
		{
			assert(device && "Device is not Valid!");

			D3D11_BUFFER_DESC desc = {};
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.ByteWidth = sizeof(InstanceData) * MAX_INSTANCES;
			device->CreateBuffer(&desc, nullptr, &instanceBuffer);
		}

		void DX11Backend::UpdateInstanceBuffer(const std::vector<InstanceData>& instances)
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			context->Map(instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, instances.data(), instances.size() * sizeof(InstanceData));
			context->Unmap(instanceBuffer.Get(), 0);
		}

	}
}
