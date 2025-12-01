#include "Graphics/DX11/DX11ShaderManager.h"

#include <cassert>

#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		std::filesystem::path ShaderManager::Canonicalize(const std::wstring& w) {
			// weakly_canonical は存在しないパスでも正規化してくれる
			std::error_code ec{};
			auto p = std::filesystem::path(w);
			auto c = std::filesystem::weakly_canonical(p, ec);
			return ec ? p : c;
		}

		size_t ShaderManager::MakeKey(const ShaderCreateDesc& desc) const {
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

		std::optional<ShaderHandle> ShaderManager::FindExisting(const ShaderCreateDesc& desc) noexcept {
			const size_t k = MakeKey(desc);
			if (auto it = keyToHandle.find(k); it != keyToHandle.end())
				return it->second;
			return std::nullopt;
		}

		void ShaderManager::RegisterKey(const ShaderCreateDesc& desc, ShaderHandle h) {
			const size_t k = MakeKey(desc);
			keyToHandle.emplace(k, h);
		}

		ShaderData ShaderManager::CreateResource(const ShaderCreateDesc& desc, ShaderHandle h)
		{
			ShaderData shader{};

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

			// === Reflection (VS for InputLayout) ===
			ReflectInputLayout(vsBlob.Get(), shader.inputLayoutDesc, shader.inputLayoutSemanticNames, shader);

			// === Reflection (VS for bindings) ===
			ReflectShaderResources(vsBlob.Get(), shader.vsBindings);

			// === Compile PS ===
			Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
			if (desc.psPath.empty()) return shader; // ピクセルシェーダー無し許容

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

			// === Reflection (PS for bindings) ===
			ReflectShaderResources(psBlob.Get(), shader.psBindings);

			return shader;
		}

		inline bool IsInstanceSemantic(const std::string& semanticName) {
			return semanticName.starts_with(ShaderManager::INSTANCE_SEMANTIC_NAME);
		}

		// System-Value(SV_*) は IA ではなくパイプラインが自動供給する → InputLayout から除外
		static inline bool IsIAConsumed(const D3D11_SIGNATURE_PARAMETER_DESC& p) noexcept
		{
			// D3D_NAME_UNDEFINED … 通常セマンティク（POSITION/NORMAL/TEXCOORD/…）
			// それ以外（SV_Position, SV_VertexID, SV_InstanceID など）は IA 非対象
			return p.SystemValueType == D3D_NAME_UNDEFINED;
		}

		// “TEXCOORD” のように Index が省略/0 の表記ゆれ対策（任意）
		static inline bool IsTexcoord(std::string_view s) noexcept {
			return s == "TEXCOORD" || s.rfind("TEXCOORD", 0) == 0;
		}

		void ShaderManager::ReflectInputLayout(ID3DBlob* vsBlob,
			std::vector<D3D11_INPUT_ELEMENT_DESC>& outDesc,
			std::vector<std::string>& semanticNames,
			ShaderData& currentShaderData)
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

			bool allKnown = true;
			std::vector<SemanticKey> required;

			for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
				D3D11_SIGNATURE_PARAMETER_DESC p{};
				reflector->GetInputParameterDesc(i, &p);

				// 1. SV_* は IA で供給しない → 完全スキップ（requiredInputs にも入れない）
				if (!IsIAConsumed(p)) {
					continue;
				}

				// 以降は IA が消費する通常入力だけ
				semanticNames.emplace_back(p.SemanticName);
				auto& s = semanticNames.back();
				const UINT semIndex = p.SemanticIndex;

				required.push_back({ s, semIndex });

				// 既知 or オーバーライド確認（※ここで “未知” フラグを立てる）
				const bool known = IsKnownSemantic(s) || overrides_.count({ s, semIndex }) > 0;
				allKnown = allKnown && known;

				D3D11_INPUT_ELEMENT_DESC e{};
				e.SemanticName = s.c_str();
				e.SemanticIndex = semIndex;

				// 初期 Format 推定（後でセマンティクで上書き）
				if (p.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) {
					UINT n = (p.Mask & 0x8 ? 4 : p.Mask & 0x4 ? 3 : p.Mask & 0x2 ? 2 : 1);
					e.Format = (n == 4) ? DXGI_FORMAT_R32G32B32A32_FLOAT
						: (n == 3) ? DXGI_FORMAT_R32G32B32_FLOAT
						: DXGI_FORMAT_R32G32_FLOAT;
				}
				else if (p.ComponentType == D3D_REGISTER_COMPONENT_UINT32) {
					// 例：COLOR を uint 系で受けるケースに備えた初期値
					e.Format = DXGI_FORMAT_R32G32B32A32_UINT;
				}
				else if (p.ComponentType == D3D_REGISTER_COMPONENT_SINT32) {
					e.Format = DXGI_FORMAT_R32G32B32A32_SINT;
				}
				else {
					e.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				}

				// 2. オーバーライドがあれば最優先
				if (auto it = overrides_.find({ s, semIndex }); it != overrides_.end()) {
					const auto& ov = it->second;
					e.InputSlot = ov.slot;
					e.AlignedByteOffset = ov.alignedByteOffset;
					e.InputSlotClass = ov.slotClass;
					e.InstanceDataStepRate = ov.stepRate;
					e.Format = ov.format;
				}
				else {
					// 3. 既知セマンティクは規約の Slot/Format を適用
					e.InputSlot = DecideInputSlotFromSemantic(s, semIndex);
					e.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
					e.InputSlotClass = (s.rfind("INSTANCE_", 0) == 0)
						? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
					e.InstanceDataStepRate = (e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA) ? 1 : 0;

					// 既知の圧縮形式に揃える（固定マップに合わせて）
					if (s == "TANGENT")         e.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
					else if (s == "NORMAL")     e.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
					else if (IsTexcoord(s))     e.Format = DXGI_FORMAT_R16G16_FLOAT; // TEXCOORD*, half2
					else if (s == "BLENDINDICES") e.Format = DXGI_FORMAT_R8G8B8A8_UINT;
					else if (s == "BLENDWEIGHT")  e.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					// COLOR は運用方針で：float4運用→UNORM、uint4運用→UINT
					else if (s == "COLOR")      e.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // or R8G8B8A8_UINT
				}

				outDesc.push_back(e);
			}

			// 4. モード判定は “IA入力だけ” を基準に行う
			auto& sd = currentShaderData;
			sd.requiredInputs = std::move(required);
			sd.bindingMode = allKnown ? InputBindingMode::BINDMODE_AUTOSTREAMS
				: (!overrides_.empty() ? InputBindingMode::BINDMODE_OVERRIDEMAP
					: InputBindingMode::BINDMODE_LEGACYMANUAL);
		}

		unsigned int ShaderManager::DecideInputSlotFromSemantic(std::string_view name, unsigned int semanticIndex) noexcept
		{
			// ざっくり規約：
			// POSITION        -> slot 0
			// TANGENT		   -> slot 1（同VB内オフセットで共存可）
			// NORMAL		   -> slot 5（R8G8B8A8_SNORMのため,わける）
			// TEXCOORD*       -> slot 2（uv0/uv1 もここ）
			// BLEND*          -> slot 3（スキニング）
			// INSTANCE_*      -> slot 4 以降でも良いが、簡単に 1 に寄せたいならここで固定しない
			// 今回は INSTANCE_* は Reflect 内で PER_INSTANCE に切替＋slot=1固定にする（既存挙動に沿う）
			if (name == "POSITION") return 0;
			if (name == "TANGENT") return 1;  // TANGENT: slot1
			if (name == "NORMAL")  return 5;  // NORMAL : slot5（VBを分けた場合）
			if (name == "TEXCOORD") return 2; // *Index は InputElement の SemanticIndex に入る
			if (name == "BLENDINDICES" || name == "BLENDWEIGHT") return 3;
			// その他は0へ（カスタムは適宜拡張）
			return 0;
		}

		void ShaderManager::ReflectShaderResources(ID3DBlob* blob, std::vector<ShaderResourceBinding>& outBindings)
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

		bool ShaderManager::IsKnownSemantic(std::string_view s) const noexcept {
			// 必要なら小文字化
			if (s == "POSITION" || s == "NORMAL" || s == "TANGENT" || s == "BLENDINDICES" || s == "BLENDWEIGHT") return true;
			if (s.rfind("TEXCOORD", 0) == 0) return true;     // TEXCOORD0,1,...
			if (s.rfind("INSTANCE_", 0) == 0) return true;    // INSTANCE_MAT* など
			return false;
		}

		void ShaderManager::RegisterSemanticOverride(const SemanticKey& key, const SemanticBinding& bind) {
			overrides_[key] = bind;
		}
	}
}