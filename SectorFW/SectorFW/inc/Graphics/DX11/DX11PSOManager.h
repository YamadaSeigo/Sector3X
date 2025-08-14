#pragma once

#include <optional>
#include <unordered_map>

#include "DX11ShaderManager.h"

namespace SectorFW
{
	namespace Graphics
	{
		struct DX11PSOCreateDesc {
			ShaderHandle shader;
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
		};

		struct DX11PSOData {
			ComPtr<ID3D11InputLayout> inputLayout = nullptr;
			ShaderHandle shader;
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
		};

		class DX11PSOManager : public ResourceManagerBase<DX11PSOManager, PSOHandle, DX11PSOCreateDesc, DX11PSOData> {
		public:
			explicit DX11PSOManager(ID3D11Device* device, DX11ShaderManager* shaderMgr)
				: device(device), shaderManager(shaderMgr) {
			}

			//=== ResourceManagerBase Ç™åƒÇ‘ÉtÉbÉN ===
			std::optional<PSOHandle> FindExisting(const DX11PSOCreateDesc& desc);
			void RegisterKey(const DX11PSOCreateDesc& desc, PSOHandle h);

			DX11PSOData CreateResource(const DX11PSOCreateDesc& desc, PSOHandle h);

		private:
			ID3D11Device* device;
			DX11ShaderManager* shaderManager;

			// ShaderHandle.index Å® PSOHandle
			std::unordered_map<uint32_t, PSOHandle> shaderToPSO_;
		};
	}
}
