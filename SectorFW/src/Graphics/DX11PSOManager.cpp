#include "Graphics/DX11/DX11PSOManager.h"
#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		std::optional<PSOHandle> PSOManager::FindExisting(const PSOCreateDesc& desc) noexcept {
			auto it = shaderToPSO_.find(desc.shader.index);
			if (it != shaderToPSO_.end()) return it->second;
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