/*****************************************************************//**
 * @file   DX11ShaderManager.h
 * @brief DirectX 11のシェーダーマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <d3d11shader.h>

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "Util/ResouceManagerBase.hpp"

#include <filesystem>
#include <unordered_map>

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief シェーダーハンドルの型
		 */
		enum class InputBindingMode { AutoStreams, LegacyManual, OverrideMap };
		/**
		 * @brief セマンティクキーを表す構造体
		 */
		struct SemanticKey {
			std::string name;
			UINT index;
			bool operator==(const SemanticKey& o) const { return name == o.name && index == o.index; }
		};
		/**
		 * @brief セマンティクキーのハッシュ関数
		 */
		struct SemanticKeyHash {
			size_t operator()(SemanticKey const& k) const noexcept {
				return std::hash<std::string>()(k.name) ^ (k.index * 1315423911u);
			}
		};
		/**
		 * @brief セマンティクバインディングを表す構造体
		 */
		struct SemanticBinding {
			UINT slot = 0;
			DXGI_FORMAT format = DXGI_FORMAT_R32G32B32_FLOAT;
			UINT alignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
			D3D11_INPUT_CLASSIFICATION slotClass = D3D11_INPUT_PER_VERTEX_DATA;
			UINT stepRate = 0;
		};
		/**
		 * @brief シェーダーステージを表す列挙型
		 */
		struct ShaderResourceBinding {
			std::string name;
			UINT bindPoint;
			D3D_SHADER_INPUT_TYPE type; // D3D_SIT_TEXTURE, D3D_SIT_CBUFFERなど
			D3D_SHADER_INPUT_FLAGS flags;
			ShaderStage stage;
		};
		/**
		 * @brief DirectX 11のシェーダー作成情報を定義する構造体
		 */
		struct ShaderCreateDesc {
			MaterialTemplateID templateID = MaterialTemplateID::PBR;
			std::wstring vsPath;
			std::wstring psPath;
		};
		/**
		 * @brief DirectX 11のシェーダーデータを定義する構造体
		 */
		struct ShaderData {
			MaterialTemplateID templateID;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
			Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
			std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;
			std::vector<std::string> inputLayoutSemanticNames; // セマンティック名のリスト
			std::vector<ShaderResourceBinding> psBindings; // SRV / CBV info
			std::vector<ShaderResourceBinding> vsBindings; // SRV / CBV info

			InputBindingMode bindingMode = InputBindingMode::AutoStreams;
			std::vector<SemanticKey> requiredInputs;
		};
		/**
		 * @brief DirectX 11のシェーダーマネージャークラス
		 */
		class ShaderManager : public ResourceManagerBase<ShaderManager, ShaderHandle, ShaderCreateDesc, ShaderData> {
		public:
			/**
			 * @brief インスタンスセマンティック名
			 */
			static inline constexpr const char* INSTANCE_SEMANTIC_NAME = "INSTANCE_";
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイスインターフェース
			 */
			explicit ShaderManager(ID3D11Device* device) noexcept : device(device) {}
			/**
			 * @brief 既存のシェーダーを検索する関数
			 * @param desc シェーダー作成情報
			 * @return std::optional<ShaderHandle> 既存のシェーダーハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<ShaderHandle> FindExisting(const ShaderCreateDesc& desc) noexcept;
			/**
			 * @brief シェーダーのキーとハンドルを登録する関数
			 * @param desc シェーダー作成情報
			 * @param h 登録するシェーダーハンドル
			 */
			void RegisterKey(const ShaderCreateDesc& desc, ShaderHandle h);
			/**
			 * @brief シェーダーリソースを作成する関数
			 * @param desc シェーダー作成情報
			 * @param h 登録するシェーダーハンドル
			 * @return DX11ShaderData 作成されたシェーダーデータ
			 */
			ShaderData CreateResource(const ShaderCreateDesc& desc, ShaderHandle h);
		private:
			void ReflectInputLayout(ID3DBlob* vsBlob,
				std::vector<D3D11_INPUT_ELEMENT_DESC>& outDesc,
				std::vector<std::string>& semanticNames,
				ShaderData& currentShaderData);

			// セマンティク名から InputSlot を決める簡易ポリシー
			static unsigned int DecideInputSlotFromSemantic(std::string_view name, unsigned int semanticIndex) noexcept;

			//=== キー計算 ===
			size_t MakeKey(const ShaderCreateDesc& desc) const;
			static std::filesystem::path Canonicalize(const std::wstring& w);
			static inline void HashCombine(size_t& seed, size_t v) {
				// 64bit対応の簡易コンバイン
				seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
			}

			void ReflectShaderResources(ID3DBlob* blob, std::vector<ShaderResourceBinding>& outBindings);

			bool IsKnownSemantic(std::string_view s) const noexcept;
			void RegisterSemanticOverride(const SemanticKey& key, const SemanticBinding& bind);

		private:
			ID3D11Device* device;
			// キー -> ハンドル の検索表
			std::unordered_map<size_t, ShaderHandle> keyToHandle;

			// 内部テーブル
			std::unordered_map<SemanticKey, SemanticBinding, SemanticKeyHash> overrides_;
		};
	}
}
