#include "Graphics/DX11/DX11PSOManager.h"
#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics
	{
		std::optional<PSOHandle> DX11PSOManager::FindExisting(const DX11PSOCreateDesc& desc) noexcept {
			auto it = shaderToPSO_.find(desc.shader.index);
			if (it != shaderToPSO_.end()) return it->second;
			return std::nullopt;
		}

		void DX11PSOManager::RegisterKey(const DX11PSOCreateDesc& desc, PSOHandle h) {
			shaderToPSO_.emplace(desc.shader.index, h);
		}

		DX11PSOData DX11PSOManager::CreateResource(const DX11PSOCreateDesc& desc, PSOHandle h)
		{
			DX11PSOData pso{};
			{
				auto shaderData = shaderManager->Get(desc.shader);

				HRESULT hr = device->CreateInputLayout(shaderData.ref().inputLayoutDesc.data(),
					(UINT)shaderData.ref().inputLayoutDesc.size(),
					shaderData.ref().vsBlob->GetBufferPointer(),
					shaderData.ref().vsBlob->GetBufferSize(),
					&pso.inputLayout);

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