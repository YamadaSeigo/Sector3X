#pragma once

#include <d3d11shader.h>

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "../ResouceManagerBase.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		struct ShaderResourceBinding {
			std::string name;
			UINT bindPoint;
			D3D_SHADER_INPUT_TYPE type; // D3D_SIT_TEXTURE, D3D_SIT_CBUFFERなど
			D3D_SHADER_INPUT_FLAGS flags;
		};

		struct DX11ShaderCreateDesc {
			MaterialTemplateID templateID = MaterialTemplateID::PBR;
			std::wstring vsPath;
			std::wstring psPath;
		};

		struct DX11ShaderData {
			MaterialTemplateID templateID;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
			Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
			std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;
			std::vector<std::string> inputLayoutSemanticNames; // セマンティック名のリスト
			std::vector<ShaderResourceBinding> bindings; // SRV / CBV info
		};

		class DX11ShaderManager : public ResourceManagerBase<DX11ShaderManager, ShaderHandle, DX11ShaderCreateDesc, DX11ShaderData> {
		public:
			static inline constexpr const char* INSTANCE_SEMANTIC_NAME = "INSTANCE_";

			explicit DX11ShaderManager(ID3D11Device* device) : device(device) {}

			DX11ShaderData CreateResource(const DX11ShaderCreateDesc& desc);

			const std::vector<ShaderResourceBinding>& GetBindings(ShaderHandle handle) const {
				return Get(handle).bindings;
			}

		private:
			void ReflectInputLayout(ID3DBlob* vsBlob, 
				std::vector<D3D11_INPUT_ELEMENT_DESC>& outDesc,
				std::vector<std::string>& semanticNames);

			void ReflectShaderResources(ID3DBlob* psBlob, std::vector<ShaderResourceBinding>& outBindings);

		private:
			ID3D11Device* device;
		};
	}
}
