#pragma once

#include <d3d11shader.h>

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "Util/ResouceManagerBase.hpp"

#include <filesystem>
#include <unordered_map>

namespace SectorFW
{
	namespace Graphics
	{
		struct ShaderResourceBinding {
			std::string name;
			UINT bindPoint;
			D3D_SHADER_INPUT_TYPE type; // D3D_SIT_TEXTURE, D3D_SIT_CBUFFERなど
			D3D_SHADER_INPUT_FLAGS flags;
			ShaderStage stage;
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
			std::vector<ShaderResourceBinding> psBindings; // SRV / CBV info
			std::vector<ShaderResourceBinding> vsBindings; // SRV / CBV info
		};

		class DX11ShaderManager : public ResourceManagerBase<DX11ShaderManager, ShaderHandle, DX11ShaderCreateDesc, DX11ShaderData> {
		public:
			static inline constexpr const char* INSTANCE_SEMANTIC_NAME = "INSTANCE_";

			explicit DX11ShaderManager(ID3D11Device* device) : device(device) {}

			std::optional<ShaderHandle> FindExisting(const DX11ShaderCreateDesc& desc);
			void RegisterKey(const DX11ShaderCreateDesc& desc, ShaderHandle h);

			DX11ShaderData CreateResource(const DX11ShaderCreateDesc& desc, ShaderHandle h);

			const std::vector<ShaderResourceBinding>& GetPSBindings(ShaderHandle handle) const {
				return Get(handle).psBindings;
			}
			const std::vector<ShaderResourceBinding>& GetVSBindings(ShaderHandle handle) const {
				return Get(handle).vsBindings;
			}

		private:
			void ReflectInputLayout(ID3DBlob* vsBlob,
				std::vector<D3D11_INPUT_ELEMENT_DESC>& outDesc,
				std::vector<std::string>& semanticNames);

			//=== キー計算 ===
			size_t MakeKey(const DX11ShaderCreateDesc& desc) const;
			static std::filesystem::path Canonicalize(const std::wstring& w);
			static inline void HashCombine(size_t& seed, size_t v) {
				// 64bit対応の簡易コンバイン
				seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
			}

			void ReflectShaderResources(ID3DBlob* blob, std::vector<ShaderResourceBinding>& outBindings);

		private:
			ID3D11Device* device;
			// キー -> ハンドル の検索表
			std::unordered_map<size_t, ShaderHandle> keyToHandle;
		};
	}
}
