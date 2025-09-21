/*****************************************************************//**
 * @file   DX11MeshManager.h
 * @brief DirectX 11のメッシュマネージャーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "DX11MaterialManager.h"
#include "DX11TextureManager.h"

#include <string>

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief メッシュ作成のための構造体
		 */
		struct DX11MeshCreateDesc {
			const void* vertices = nullptr;
			uint32_t vSize = {};
			uint32_t stride = {};
			const uint32_t* indices = nullptr;
			uint32_t iSize = {};
			D3D11_USAGE vUsage = D3D11_USAGE_IMMUTABLE;
			D3D11_USAGE iUsage = D3D11_USAGE_IMMUTABLE;
			D3D11_CPU_ACCESS_FLAG cpuAccessFlags = D3D11_CPU_ACCESS_WRITE; // D3D11_USAGE_STAGING用
			std::wstring sourcePath;
		};
		/**
		 * @brief DirectX 11のメッシュデータを定義する構造体
		 */
		struct DX11MeshData {
			ComPtr<ID3D11Buffer> vb = nullptr;
			ComPtr<ID3D11Buffer> ib = nullptr;
			uint32_t indexCount = 0;
			uint32_t stride = 0;
		private:
			std::wstring path; // キャッシュ用パス

			friend class DX11MeshManager;
		};
		/**
		 * @brief DirectX 11のメッシュマネージャークラス
		 */
		class DX11MeshManager : public ResourceManagerBase<DX11MeshManager, MeshHandle, DX11MeshCreateDesc, DX11MeshData> {
		public:
			/**
			 * @brief コンストラクタ
			 * @param dev DirectX 11のデバイス
			 */
			explicit DX11MeshManager(ID3D11Device* dev) noexcept : device(dev) {}
			/**
			 * @brief 既存のメッシュを検索する関数
			 * @param d メッシュ作成のための構造体
			 * @return std::optional<MeshHandle> 既存のメッシュハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<MeshHandle> FindExisting(const DX11MeshCreateDesc& d) noexcept {
				if (!d.sourcePath.empty()) {
					if (auto it = pathToHandle.find(d.sourcePath); it != pathToHandle.end())
						return it->second;
				}
				return std::nullopt;
			}
			/**
			 * @brief メッシュハンドルをキャッシュに登録する関数
			 * @param d メッシュ作成のための構造体
			 * @param h メッシュハンドル
			 */
			void RegisterKey(const DX11MeshCreateDesc& d, MeshHandle h) {
				if (!d.sourcePath.empty()) pathToHandle.emplace(d.sourcePath, h);
			}
			/**
			 * @brief メッシュリソースを作成する関数
			 * @param desc メッシュ作成のための構造体
			 * @param h メッシュハンドル
			 * @return DX11MeshData 作成されたメッシュデータ
			 */
			DX11MeshData CreateResource(const DX11MeshCreateDesc& desc, MeshHandle h);
			/**
			 * @brief キャッシュからメッシュハンドルを削除する関数
			 * @param idx 削除するメッシュのインデックス
			 */
			void RemoveFromCaches(uint32_t idx);
			/**
			 * @brief メッシュリソースを破棄する関数
			 * @param idx 破棄するメッシュのインデックス
			 * @param currentFrame 現在のフレーム番号
			 */
			void DestroyResource(uint32_t idx, uint64_t currentFrame);
			/**
			 * @brief メッシュのインデックス数を設定する関数
			 * @param h メッシュハンドル
			 * @param count インデックス数
			 */
			void SetIndexCount(MeshHandle h, uint32_t count) {
				if (!IsValid(h)) {
					assert(false && "Invalid MeshHandle in SetIndexCount");
					return;
				}
				std::unique_lock lock(mapMutex);
				slots[h.index].data.indexCount = count;
			}
		private:
			ID3D11Device* device;

			std::unordered_map<std::wstring, MeshHandle> pathToHandle;
		};
	}
}
