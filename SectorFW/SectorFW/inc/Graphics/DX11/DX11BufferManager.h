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
		struct DX11BufferCreateDesc {
			std::string name;
			size_t size = {};
		};

		struct DX11BufferData {
			ComPtr<ID3D11Buffer> buffer;
			std::string_view name;
		};

		struct DX11BufferCacheKey {
			size_t hash;
			size_t size;

			bool operator==(const DX11BufferCacheKey& other) const {
				return hash == other.hash && size == other.size;
			}
		};

		struct DX11BufferCacheKeyHash {
			std::size_t operator()(const DX11BufferCacheKey& k) const {
				return std::hash<size_t>()(k.hash) ^ std::hash<size_t>()(k.size);
			}
		};

		struct DX11BufferUpdateDesc {
			BufferHandle handle = {};
			const void* data = nullptr;
			size_t size = {};
			bool isDelete = true;

			bool operator==(const DX11BufferUpdateDesc& other) const {
				return handle.index == other.handle.index;
			}
		};

		class DX11BufferManager : public ResourceManagerBase<
			DX11BufferManager, BufferHandle, DX11BufferCreateDesc, DX11BufferData>
		{
		public:
			DX11BufferManager(ID3D11Device* device, ID3D11DeviceContext* context)
				: device(device), context(context) {
			}

			// 既存検索（名前ベース）
			std::optional<BufferHandle> FindExisting(const DX11BufferCreateDesc& desc) {
				if (auto it = nameToHandle.find(desc.name); it != nameToHandle.end())
					return it->second;
				return std::nullopt;
			}

			// キー登録
			void RegisterKey(const DX11BufferCreateDesc& desc, BufferHandle h) {
				nameToHandle.emplace(desc.name, h);
			}

			DX11BufferData CreateResource(const DX11BufferCreateDesc& desc, BufferHandle h) {
				DX11BufferData cb{};

				D3D11_BUFFER_DESC bd = {};
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				bd.ByteWidth = static_cast<UINT>(desc.size);

				HRESULT hr = device->CreateBuffer(&bd, nullptr, &cb.buffer);
				if (FAILED(hr)) {
					assert(false && "Failed to create constant buffer");
				}

				return cb;
			}

			BufferHandle FindByName(const std::string& name) const {
				auto it = nameToHandle.find(name);
				if (it != nameToHandle.end()) return it->second;
				assert(false && "ConstantBuffer not found");
				return {};
			}

			// 自動CB(内容キャッシュ)は AcquireAPI で
			BufferHandle AcquireWithContent(const void* data, size_t size) {
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
				auto& d = Get(h);
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(d.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				memcpy(m.pData, data, size);
				context->Unmap(d.buffer.Get(), 0);

				cbvCache[key] = h;
				handleToCacheKey[h.index] = key;
				return h;
			}

			void UpdateConstantBuffer(const DX11BufferUpdateDesc& desc) {
				std::lock_guard<std::mutex> lock(updateMutex);
				pendingUpdates.push_back(desc);
			}

			void PendingUpdates() {
				if (!pendingUpdates.empty()) {
					auto it = std::unique(pendingUpdates.begin(), pendingUpdates.end());
					pendingUpdates.erase(it, pendingUpdates.end());

					for (const auto& update : pendingUpdates) {
						auto& data = Get(update.handle);
						D3D11_MAPPED_SUBRESOURCE mapped;
						HRESULT hr = context->Map(data.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
						if (SUCCEEDED(hr)) {
							memcpy(mapped.pData, update.data, update.size);
							context->Unmap(data.buffer.Get(), 0);
						}
						else {
							assert(false && "Failed to map constant buffer for update");
						}
						if (update.data && update.isDelete) delete update.data;
					}

					pendingUpdates.clear();
				}
			}

			// RemoveFromCaches: 名前表 / 内容キャッシュを掃除
			void RemoveFromCaches(uint32_t idx) {
				auto& d = slots[idx].data;
				if (!std::string(d.name).empty()) nameToHandle.erase(std::string(d.name));
				if (auto it = handleToCacheKey.find(idx); it != handleToCacheKey.end()) {
					cbvCache.erase(it->second);
					handleToCacheKey.erase(it);
				}
			}

			// DestroyResource: GPU 解放
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
