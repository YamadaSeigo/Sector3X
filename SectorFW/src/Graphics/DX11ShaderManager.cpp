#include "Graphics/DX11/DX11ShaderManager.h"

#include <cassert>

#include "Debug/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		std::filesystem::path DX11ShaderManager::Canonicalize(const std::wstring& w) {
			// weakly_canonical は存在しないパスでも正規化してくれる
			std::error_code ec{};
			auto p = std::filesystem::path(w);
			auto c = std::filesystem::weakly_canonical(p, ec);
			return ec ? p : c;
		}

		size_t DX11ShaderManager::MakeKey(const DX11ShaderCreateDesc& desc) const {
			// パスは正規化 & lowercase（Windows想定の大文字小文字差異の吸収）
			auto vsCan = Canonicalize(desc.vsPath).generic_wstring();
			auto psCan = Canonicalize(desc.psPath).generic_wstring();
			std::transform(vsCan.begin(), vsCan.end(), vsCan.begin(), ::towlower);
			std::transform(psCan.begin(), psCan.end(), psCan.begin(), ::towlower);

			std::hash<std::wstring> Hs;
			size_t seed = 0;
			HashCombine(seed, static_cast<size_t>(desc.templateID));
			HashCombine(seed, Hs(vsCan));
			HashCombine(seed, Hs(psCan));
			return seed;
		}

		std::optional<ShaderHandle> DX11ShaderManager::FindExisting(const DX11ShaderCreateDesc& desc) {
			const size_t k = MakeKey(desc);
			if (auto it = keyToHandle.find(k); it != keyToHandle.end())
				return it->second;
			return std::nullopt;
		}

		void DX11ShaderManager::RegisterKey(const DX11ShaderCreateDesc& desc, ShaderHandle h) {
			const size_t k = MakeKey(desc);
			keyToHandle.emplace(k, h);
		}

		DX11ShaderData DX11ShaderManager::CreateResource(const DX11ShaderCreateDesc& desc, ShaderHandle h)
		{
			DX11ShaderData shader{};

			shader.templateID = desc.templateID;

			HRESULT hr;

			// === Compile VS ===
			Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
			hr = D3DReadFileToBlob(desc.vsPath.c_str(), vsBlob.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to compile vertex shader: %s", desc.vsPath.c_str());
				assert(false && "Failed to compile vertex shader");
			}

			hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &shader.vs);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create vertex shader: %s", desc.vsPath.c_str());
				assert(false && "Failed to create vertex shader");
			}

			shader.vsBlob = vsBlob;

			// === Compile PS ===
			Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
			hr = D3DReadFileToBlob(desc.psPath.c_str(), psBlob.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to compile pixel shader: %s", desc.psPath.c_str());
				assert(false && "Failed to compile pixel shader");
			}

			hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &shader.ps);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create pixel shader: %s", desc.psPath.c_str());
				assert(false && "Failed to create pixel shader");
			}

			// === Reflection (VS for InputLayout) ===
			ReflectInputLayout(vsBlob.Get(), shader.inputLayoutDesc, shader.inputLayoutSemanticNames);

			// === Reflection (VS for bindings) ===
			ReflectShaderResources(vsBlob.Get(), shader.vsBindings);

			// === Reflection (PS for bindings) ===
			ReflectShaderResources(psBlob.Get(), shader.psBindings);

			return shader;
		}

		inline bool IsInstanceSemantic(const std::string& semanticName) {
			return semanticName.starts_with(DX11ShaderManager::INSTANCE_SEMANTIC_NAME);
		}

		void DX11ShaderManager::ReflectInputLayout(ID3DBlob* vsBlob,
			std::vector<D3D11_INPUT_ELEMENT_DESC>& outDesc,
			std::vector<std::string>& semanticNames)
		{
			outDesc.clear();
			semanticNames.clear();

			HRESULT hr;

			ComPtr<ID3D11ShaderReflection> reflector;
			hr = D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), IID_PPV_ARGS(&reflector));
			if (FAILED(hr)) {
				LOG_ERROR("Failed to reflect vertex shader: %s", hr);
				assert(false && "Failed to reflect vertex shader");
			}

			D3D11_SHADER_DESC shaderDesc;
			hr = reflector->GetDesc(&shaderDesc);

			semanticNames.reserve(shaderDesc.InputParameters); // emplace_backで再構築されないように事前にサイズを確保

			if (FAILED(hr)) {
				LOG_ERROR("Failed to get shader description: %s", hr);
				assert(false && "Failed to get shader description");
			}

			for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
				D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
				hr = reflector->GetInputParameterDesc(i, &paramDesc);
				if (FAILED(hr)) {
					LOG_ERROR("Failed to get input parameter description: %s", hr);
					assert(false && "Failed to get input parameter description");
				}

				D3D11_INPUT_ELEMENT_DESC elem{};

				// SemanticName の寿命を保証するためコピー
				semanticNames.emplace_back(paramDesc.SemanticName);
				elem.SemanticName = semanticNames.back().c_str();
				elem.SemanticIndex = paramDesc.SemanticIndex;
				elem.InputSlot = 0;
				elem.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
				elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
				elem.InstanceDataStepRate = 0;

				// フォーマット判定（ComponentType + Mask）
				const auto& mask = paramDesc.Mask;
				const auto& compType = paramDesc.ComponentType;

				if (compType == D3D_REGISTER_COMPONENT_FLOAT32) {
					if (mask == 1)
						elem.Format = DXGI_FORMAT_R32_FLOAT;
					else if (mask <= 3)
						elem.Format = DXGI_FORMAT_R32G32_FLOAT;
					else if (mask <= 7)
						elem.Format = DXGI_FORMAT_R32G32B32_FLOAT;
					else if (mask <= 15)
						elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				}
				else if (compType == D3D_REGISTER_COMPONENT_UINT32) {
					if (mask == 1)
						elem.Format = DXGI_FORMAT_R32_UINT;
					else if (mask <= 3)
						elem.Format = DXGI_FORMAT_R32G32_UINT;
					else if (mask <= 7)
						elem.Format = DXGI_FORMAT_R32G32B32_UINT;
					else if (mask <= 15)
						elem.Format = DXGI_FORMAT_R32G32B32A32_UINT;
				}
				else if (compType == D3D_REGISTER_COMPONENT_SINT32) {
					if (mask == 1)
						elem.Format = DXGI_FORMAT_R32_SINT;
					else if (mask <= 3)
						elem.Format = DXGI_FORMAT_R32G32_SINT;
					else if (mask <= 7)
						elem.Format = DXGI_FORMAT_R32G32B32_SINT;
					else if (mask <= 15)
						elem.Format = DXGI_FORMAT_R32G32B32A32_SINT;
				}

				// インスタンス用セマンティクス判定
				const std::string semName = paramDesc.SemanticName;
				if (IsInstanceSemantic(semName)) {
					elem.InputSlot = 1;
					elem.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
					elem.InstanceDataStepRate = 1;
				}

				outDesc.push_back(elem);
			}
		}
		void DX11ShaderManager::ReflectShaderResources(ID3DBlob* blob, std::vector<ShaderResourceBinding>& outBindings)
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
			D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflector));

			D3D11_SHADER_DESC shaderDesc;
			reflector->GetDesc(&shaderDesc);

			for (UINT i = 0; i < shaderDesc.BoundResources; i++) {
				D3D11_SHADER_INPUT_BIND_DESC bindDesc;
				reflector->GetResourceBindingDesc(i, &bindDesc);

				ShaderResourceBinding binding{};
				binding.name = bindDesc.Name;
				binding.bindPoint = bindDesc.BindPoint;
				binding.type = bindDesc.Type;
				binding.flags = static_cast<D3D_SHADER_INPUT_FLAGS>(bindDesc.uFlags);

				outBindings.push_back(binding);
			}
		}
	}
}