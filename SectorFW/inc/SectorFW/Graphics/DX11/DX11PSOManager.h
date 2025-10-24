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
	namespace Graphics
	{
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）を作成するための記述子構造体
		 */
		struct DX11PSOCreateDesc {
			ShaderHandle shader;
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
		};
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）のデータ構造体
		 */
		struct DX11PSOData {
			ComPtr<ID3D11InputLayout> inputLayout = nullptr;
			ShaderHandle shader;
			RasterizerStateID rasterizerState = RasterizerStateID::SolidCullBack;
		};
		/**
		 * @brief DirectX 11のパイプラインステートオブジェクト（PSO）を管理するクラス
		 */
		class DX11PSOManager : public ResourceManagerBase<DX11PSOManager, PSOHandle, DX11PSOCreateDesc, DX11PSOData> {
		public:
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイス
			 * @param shaderMgr シェーダーマネージャー
			 */
			explicit DX11PSOManager(ID3D11Device* device, DX11ShaderManager* shaderMgr) noexcept
				: device(device), shaderManager(shaderMgr) {
			}

			/**
			 * @brief ResourceManagerBase が呼ぶフック
			 * @param desc PSOの作成記述子
			 * @return std::optional<PSOHandle> 既存のPSOハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<PSOHandle> FindExisting(const DX11PSOCreateDesc& desc) noexcept;
			void RegisterKey(const DX11PSOCreateDesc& desc, PSOHandle h);
			/**
			 * @brief 新しいPSOリソースを作成する関数
			 * @param desc PSOの作成記述子
			 * @param h PSOハンドル
			 * @return DX11PSOData 作成されたPSOデータ
			 */
			DX11PSOData CreateResource(const DX11PSOCreateDesc& desc, PSOHandle h);

		private:
			ID3D11Device* device;
			DX11ShaderManager* shaderManager;

			// ShaderHandle.index → PSOHandle
			std::unordered_map<uint32_t, PSOHandle> shaderToPSO_;
		};
	}
}
