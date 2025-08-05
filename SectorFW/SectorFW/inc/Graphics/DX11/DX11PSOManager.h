#pragma once

#include "DX11ShaderManager.h"

namespace SectorFW
{
	namespace Graphics
	{
		struct DX11PSOCreateDesc {
			ShaderHandle shader;
		};

		struct DX11PSOData {
			ComPtr<ID3D11InputLayout> inputLayout = nullptr;
			ShaderHandle shader;
		};

		class DX11PSOManager : public ResourceManagerBase<DX11PSOManager, PSOHandle, DX11PSOCreateDesc, DX11PSOData> {
		public:
			explicit DX11PSOManager(ID3D11Device* device, DX11ShaderManager* shaderMgr)
				: device(device), shaderManager(shaderMgr) {
			}

			DX11PSOData CreateResource(const DX11PSOCreateDesc& desc);

		private:
			ID3D11Device* device;
			DX11ShaderManager* shaderManager;
		};
	}
}
