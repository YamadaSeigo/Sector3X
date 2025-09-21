/*****************************************************************//**
 * @file   DX11TextureManager.h
 * @brief DirectX 11のテクスチャマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "Util/ResouceManagerBase.hpp"

#include <unordered_map>
#include <mutex>
#include <optional>

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX 11のテクスチャを作成するための構造体
		 */
		struct DX11TextureCreateDesc {
			std::string path;
			bool forceSRGB = false;
		};
		/**
		 * @brief DirectX 11のテクスチャデータを格納する構造体
		 */
		struct DX11TextureData {
			ComPtr<ID3D11ShaderResourceView> srv = nullptr;
		private:
			std::string path; // キャッシュ用のパス

			friend class DX11TextureManager;
		};
		/**
		 * @brief DirectX 11のテクスチャマネージャークラス
		 */
		class DX11TextureManager : public ResourceManagerBase<
			DX11TextureManager, TextureHandle, DX11TextureCreateDesc, DX11TextureData>
		{
		public:
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイス
			 */
			explicit DX11TextureManager(ID3D11Device* device) noexcept;
			/**
			 * @brief 既存のテクスチャを検索する関数
			 * @param desc テクスチャの作成情報
			 * @return std::optional<TextureHandle> 既存のテクスチャハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<TextureHandle> FindExisting(const DX11TextureCreateDesc& desc) noexcept {
				// path を正規化してから検索するのが吉
				if (auto it = pathToHandle.find(desc.path); it != pathToHandle.end())
					return it->second;
				return std::nullopt;
			}
			/**
			 * @brief テクスチャのパスとハンドルを登録する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 */
			void RegisterKey(const DX11TextureCreateDesc& desc, TextureHandle h) {
				pathToHandle.emplace(desc.path, h);
			}
			/**
			 * @brief テクスチャリソースを作成する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 * @return DX11TextureData 作成されたテクスチャデータ
			 */
			DX11TextureData CreateResource(const DX11TextureCreateDesc& desc, TextureHandle h);
			/**
			 * @brief キャッシュからテクスチャを削除する関数
			 * @param idx 削除するテクスチャのインデックス
			 */
			void RemoveFromCaches(uint32_t idx);
			/**
			 * @brief テクスチャリソースを破棄する関数
			 * @param idx 破棄するテクスチャのインデックス
			 * @param currentFrame 現在のフレーム番号(未使用)
			 */
			void DestroyResource(uint32_t idx, uint64_t /*currentFrame*/);
		private:
			ID3D11Device* device;

			std::unordered_map<std::string, TextureHandle> pathToHandle;
		};
	}
}
