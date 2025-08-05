#include "Graphics/DX11/DX11PSOManager.h"
#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11PSOData DX11PSOManager::CreateResource(const DX11PSOCreateDesc& desc)
		{
			DX11PSOData pso{};
			auto& shaderData = shaderManager->Get(desc.shader);

			HRESULT hr = device->CreateInputLayout(shaderData.inputLayoutDesc.data(),
				(UINT)shaderData.inputLayoutDesc.size(),
				shaderData.vsBlob->GetBufferPointer(),
				shaderData.vsBlob->GetBufferSize(),
				&pso.inputLayout);

			if (FAILED(hr)) {
				LOG_ERROR("Failed to create input layout for PSO: %d", desc.shader);
				assert(false && "Failed to create input layout for PSO");
			}

			pso.shader = desc.shader;
			return pso;
		}
	}
}
