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

namespace SectorFW
{
	namespace Graphics
	{
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
		struct DX11ShaderCreateDesc {
			MaterialTemplateID templateID = MaterialTemplateID::PBR;
			std::wstring vsPath;
			std::wstring psPath;
		};
		/**
		 * @brief DirectX 11のシェーダーデータを定義する構造体
		 */
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
		/**
		 * @brief DirectX 11のシェーダーマネージャークラス
		 */
		class DX11ShaderManager : public ResourceManagerBase<DX11ShaderManager, ShaderHandle, DX11ShaderCreateDesc, DX11ShaderData> {
		public:
			/**
			 * @brief インスタンスセマンティック名
			 */
			static inline constexpr const char* INSTANCE_SEMANTIC_NAME = "INSTANCE_";
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイスインターフェース
			 */
			explicit DX11ShaderManager(ID3D11Device* device) noexcept : device(device) {}
			/**
			 * @brief 既存のシェーダーを検索する関数
			 * @param desc シェーダー作成情報
			 * @return std::optional<ShaderHandle> 既存のシェーダーハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<ShaderHandle> FindExisting(const DX11ShaderCreateDesc& desc) noexcept;
			/**
			 * @brief シェーダーのキーとハンドルを登録する関数
			 * @param desc シェーダー作成情報
			 * @param h 登録するシェーダーハンドル
			 */
			void RegisterKey(const DX11ShaderCreateDesc& desc, ShaderHandle h);
			/**
			 * @brief シェーダーリソースを作成する関数
			 * @param desc シェーダー作成情報
			 * @param h 登録するシェーダーハンドル
			 * @return DX11ShaderData 作成されたシェーダーデータ
			 */
			DX11ShaderData CreateResource(const DX11ShaderCreateDesc& desc, ShaderHandle h);
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
