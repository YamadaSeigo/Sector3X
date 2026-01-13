/*****************************************************************//**
 * @file   DX11PSOManager.h
 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）を管理するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <optional>
#include <unordered_map>

#include "DX11ShaderManager.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）を作成するための記述子構造体
		 */
		struct PSOCreateDesc {
			ShaderHandle shader;
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
			std::optional<ShaderHandle> rebindShader = std::nullopt; // シェーダーリバインド用（オプション）
		};
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）のデータ構造体
		 */
		struct PSOData {
			ComPtr<ID3D11InputLayout> inputLayout = nullptr;
			ShaderHandle shader;
			ComPtr<ID3D11InputLayout> rebindInputLayout = nullptr;		// シェーダーリバインド用（オプション）
			ShaderHandle rebindShader;									// シェーダーリバインド用（オプション）
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
		};
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）を管理するクラス
		 */
		class PSOManager : public ResourceManagerBase<PSOManager, PSOHandle, PSOCreateDesc, PSOData> {
		public:
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイス
			 * @param shaderMgr シェーダーマネージャー
			 */
			explicit PSOManager(ID3D11Device* device, ShaderManager* shaderMgr) noexcept
				: device(device), shaderManager(shaderMgr) {
			}

			/**
			 * @brief ResourceManagerBase が呼ぶフック
			 * @param desc PSOの作成記述子
			 * @return std::optional<PSOHandle> 既存のPSOハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<PSOHandle> FindExisting(const PSOCreateDesc& desc) noexcept;
			void RegisterKey(const PSOCreateDesc& desc, PSOHandle h);
			/**
			 * @brief 新しいPSOリソースを作成する関数
			 * @param desc PSOの作成記述子
			 * @param h PSOハンドル
			 * @return DX11PSOData 作成されたPSOデータ
			 */
			PSOData CreateResource(const PSOCreateDesc& desc, PSOHandle h);

		private:
			ID3D11Device* device;
			ShaderManager* shaderManager;

			// ShaderHandle.index → PSOHandle
			std::unordered_map<uint32_t, PSOHandle> shaderToPSO_;
		};
	}
}
