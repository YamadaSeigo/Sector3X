/*****************************************************************//**
 * @file   DX11BufferManager.h
 * @brief DirectX 11のバッファマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "_dx11inc.h"
#include "Util/ResouceManagerBase.hpp"
#include <unordered_map>
#include <string>
#include <typeindex>

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX 11のバッファを作成するための構造体
		 */
		struct DX11BufferCreateDesc {
			std::string name;
			uint32_t size = {};
			uint32_t structureByteStride = 0; // StructuredBuffer 用（CBV では無視される）
			void* initialData = nullptr; // 初期データ（nullptr ならゼロクリア）
			D3D11_USAGE usage = D3D11_USAGE_DYNAMIC;
			D3D11_BIND_FLAG bindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_RESOURCE_MISC_FLAG miscFlags = {};
			D3D11_CPU_ACCESS_FLAG cpuAccessFlags = D3D11_CPU_ACCESS_WRITE; // D3D11_USAGE_STAGING用
		};
		/**
		 * @brief DirectX 11のバッファデータを格納する構造体
		 */
		struct DX11BufferData {
			ComPtr<ID3D11Buffer> buffer;
			std::string_view name;
		};
		/**
		 * @brief DirectX 11のバッファのハッシュ構造体
		 */
		struct DX11BufferCacheKey {
			size_t hash;
			size_t size;

			bool operator==(const DX11BufferCacheKey& other) const {
				return hash == other.hash && size == other.size;
			}
		};
		/**
		 * @brief DirectX 11のバッファの内容をハッシュ化するための関数
		 */
		struct DX11BufferCacheKeyHash {
			std::size_t operator()(const DX11BufferCacheKey& k) const {
				return std::hash<size_t>()(k.hash) ^ std::hash<size_t>()(k.size);
			}
		};
		/**
		 * @brief DirectX 11のバッファを更新するための構造体
		 */
		struct DX11BufferUpdateDesc {
			ComPtr<ID3D11Buffer> buffer;
			const void* data = nullptr;
			size_t size = {};
			bool isDelete = true;

			bool operator==(const DX11BufferUpdateDesc& other) const {
				return buffer.Get() == other.buffer.Get();
			}
		};
		/**
		 * @brief バッファの管理クラス。DirectX 11のバッファを作成、キャッシュ、更新する機能を提供する。
		 */
		class DX11BufferManager : public ResourceManagerBase<
			DX11BufferManager, BufferHandle, DX11BufferCreateDesc, DX11BufferData>
		{
		public:
			/**
			 * @brief コンストラクタ
			 * @param device DX11のデバイス
			 * @param context　DX11のデバイスコンテキスト
			 */
			DX11BufferManager(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
				: device(device), context(context) {
			}
			/**
			 * @brief デストラクタ
			 */
			~DX11BufferManager() {
				for (auto& update : pendingUpdates) {
					if (update.data && update.isDelete) delete update.data;
				}
			}
			/**
			 * @brief 既存検索（名前ベース）
			 * @param desc バッファ作成記述子
			 * @return std::optional<BufferHandle> 既存のバッファハンドル、存在しない場合は std::nullopt
			 */
			std::optional<BufferHandle> FindExisting(const DX11BufferCreateDesc& desc) noexcept {
				if (auto it = nameToHandle.find(desc.name); it != nameToHandle.end())
					return it->second;
				return std::nullopt;
			}
			/**
			 * @brief キー登録
			 * @param desc バッファ作成記述子
			 * @param h 登録するバッファハンドル
			 */
			void RegisterKey(const DX11BufferCreateDesc& desc, BufferHandle h) {
				nameToHandle.emplace(desc.name, h);
			}
			/**
			 * @brief リソース作成
			 * @param desc バッファ作成記述子
			 * @param h 登録するバッファハンドル
			 * @return DX11BufferData 作成されたバッファデータ
			 */
			DX11BufferData CreateResource(const DX11BufferCreateDesc& desc, BufferHandle h) {
				DX11BufferData cb{};

				D3D11_BUFFER_DESC bd = {};
				bd.Usage = desc.usage;
				bd.BindFlags = desc.bindFlags;
				if (desc.usage == D3D11_USAGE_DYNAMIC) {
					bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				}
				else if (desc.usage == D3D11_USAGE_STAGING) {
					bd.CPUAccessFlags = desc.cpuAccessFlags; // CPU アクセスフラグを指定
				}
				bd.ByteWidth = static_cast<UINT>(desc.size);
				bd.MiscFlags = desc.miscFlags;
				if (bd.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
					bd.StructureByteStride = desc.structureByteStride;
				}

				HRESULT hr;
				if (desc.initialData == nullptr) {
					if (bd.Usage == D3D11_USAGE_IMMUTABLE) {
						assert(false && "Immutable buffer must have initial data.");
					}

					hr = device->CreateBuffer(&bd, nullptr, &cb.buffer);
				}
				else {
					D3D11_SUBRESOURCE_DATA initData = {};
					initData.pSysMem = desc.initialData;
					hr = device->CreateBuffer(&bd, &initData, &cb.buffer);
				}

				if (FAILED(hr)) {
					assert(false && "Failed to create constant buffer");
				}

				return cb;
			}
			/**
			 * @brief 名前でバッファを検索
			 * @param name 検索するバッファの名前
			 * @return BufferHandle 見つかったバッファハンドル、見つからなかった場合は空のハンドル
			 */
			BufferHandle FindByName(const std::string& name) const noexcept {
				auto it = nameToHandle.find(name);
				if (it != nameToHandle.end()) return it->second;
				assert(false && "ConstantBuffer not found");
				return {};
			}
			/**
			 * @brief 自動CB(内容キャッシュ)は AcquireAPI で
			 * @param data データのポインタ
			 * @param size データのサイズ
			 * @return BufferHandle 取得したバッファハンドル
			 */
			BufferHandle AcquireWithContent(const void* data, uint32_t size) {
				assert(data && size > 0);

				DX11BufferCacheKey key{ HashBufferContent(data, size), size };
				if (auto it = cbvCache.find(key); it != cbvCache.end()) {
					AddRef(it->second);
					return it->second;
				}
				// 初回作成：匿名名で Add（Add が +1 を返す）
				BufferHandle h;
				Add({ .name = "auto_cb_" + std::to_string(key.hash), .size = size }, h);

				// 中身コピー
				{
					auto d = Get(h);
					D3D11_MAPPED_SUBRESOURCE m{};
					context->Map(d.ref().buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
					memcpy(m.pData, data, size);
					context->Unmap(d.ref().buffer.Get(), 0);
				}

				cbvCache[key] = h;
				handleToCacheKey[h.index] = key;
				return h;
			}
			/**
			 * @brief バッファの内容を遅延で更新するためにキューに追加
			 * @param desc バッファ作成記述子
			 */
			void UpdateBuffer(const DX11BufferUpdateDesc& desc) {
				std::lock_guard<std::mutex> lock(updateMutex);
				pendingUpdates.push_back(desc);
			}
			/**
			 * @brief 保留中のバッファ更新を処理
			 */
			void PendingUpdates() {
				if (!pendingUpdates.empty()) {
					std::lock_guard<std::mutex> lock(updateMutex);

					auto it = std::unique(pendingUpdates.begin(), pendingUpdates.end());
					pendingUpdates.erase(it, pendingUpdates.end());

					for (const auto& update : pendingUpdates) {
						D3D11_MAPPED_SUBRESOURCE mapped;
						HRESULT hr = context->Map(update.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
						if (SUCCEEDED(hr)) [[likely]] {
							memcpy(mapped.pData, update.data, update.size);
							context->Unmap(update.buffer.Get(), 0);
						}
						else {
							assert(false && "Failed to map constant buffer for update");
						}
						if (update.data && update.isDelete) delete update.data;
					}

					pendingUpdates.clear();
				}
			}

			/**
			 * @brief RemoveFromCaches: 名前表 / 内容キャッシュを掃除
			 * @param idx ハンドルのインデックス
			 */
			void RemoveFromCaches(uint32_t idx) {
				auto& d = slots[idx].data;
				if (!std::string(d.name).empty()) nameToHandle.erase(std::string(d.name));
				if (auto it = handleToCacheKey.find(idx); it != handleToCacheKey.end()) {
					cbvCache.erase(it->second);
					handleToCacheKey.erase(it);
				}
			}
			/**
			 * @brief DestroyResource: GPU 解放
			 * @param idx ハンドルのインデックス
			 */
			void DestroyResource(uint32_t idx, uint64_t /*currentFrame*/) {
				slots[idx].data.buffer.Reset();
			}
		private:
			ID3D11Device* device;
			ID3D11DeviceContext* context;
			std::unordered_map<std::string, BufferHandle> nameToHandle;
			std::unordered_map<DX11BufferCacheKey, BufferHandle, DX11BufferCacheKeyHash> cbvCache;

			std::unordered_map<uint32_t, DX11BufferCacheKey> handleToCacheKey; // key: handle.index

			std::mutex updateMutex; // 更新用のミューテックス
			std::vector<DX11BufferUpdateDesc> pendingUpdates; // 更新待ちのデータ
		};
	}
}
