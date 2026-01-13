#include "Graphics/DX11/DX11PSOManager.h"
#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		std::optional<PSOHandle> PSOManager::FindExisting(const PSOCreateDesc& desc) noexcept {
			auto it = shaderToPSO_.find(desc.shader.index);
			if (it != shaderToPSO_.end()) {
				auto& slot = slots[it->second.index];
				if (slot.alive && slot.data.rasterizerState == desc.rasterizerState)
				{
					return it->second;
				}
			}
			return std::nullopt;
		}

		void PSOManager::RegisterKey(const PSOCreateDesc& desc, PSOHandle h) {
			shaderToPSO_.emplace(desc.shader.index, h);
		}

		PSOData PSOManager::CreateResource(const PSOCreateDesc& desc, PSOHandle h)
		{
			PSOData pso{};
			{
				auto shaderData = shaderManager->Get(desc.shader);

				HRESULT hr = device->CreateInputLayout(shaderData.ref().inputLayoutDesc.data(),
					(UINT)shaderData.ref().inputLayoutDesc.size(),
					shaderData.ref().vsBlob->GetBufferPointer(),
					shaderData.ref().vsBlob->GetBufferSize(),
					&pso.inputLayout);

				if(desc.rebindShader.has_value())
				{
					auto rebindShaderData = shaderManager->Get(desc.rebindShader.value());
					hr = device->CreateInputLayout(rebindShaderData.ref().inputLayoutDesc.data(),
						(UINT)rebindShaderData.ref().inputLayoutDesc.size(),
						rebindShaderData.ref().vsBlob->GetBufferPointer(),
						rebindShaderData.ref().vsBlob->GetBufferSize(),
						&pso.rebindInputLayout);
					pso.rebindShader = desc.rebindShader.value();
				}
				else
				{
					pso.rebindInputLayout = pso.inputLayout;
					pso.rebindShader = desc.shader;
				}

				if (FAILED(hr)) {
					LOG_ERROR("Failed to create input layout for PSO: %d", desc.shader);
					assert(false && "Failed to create input layout for PSO");
				}
			}

			pso.shader = desc.shader;
			pso.rasterizerState = desc.rasterizerState;
			return pso;
		}
	}
}