#pragma once

#include "_dx11inc.h"
#include "../ResouceManagerBase.hpp"
#include <unordered_map>
#include <string>

namespace SectorFW
{
	namespace Graphics
	{
        struct DX11ConstantBufferCreateDesc {
            std::string name;
            size_t size;
        };

        struct DX11ConstantBufferData {
            ID3D11Buffer* buffer;
            std::string name;
        };

        struct DX11CBVCacheKey {
            size_t hash;
            size_t size;

            bool operator==(const DX11CBVCacheKey& other) const {
                return hash == other.hash && size == other.size;
            }
        };

        struct DX11CBVCacheKeyHash {
            std::size_t operator()(const DX11CBVCacheKey& k) const {
                return std::hash<size_t>()(k.hash) ^ std::hash<size_t>()(k.size);
            }
        };

        class DX11ConstantBufferManager : public ResourceManagerBase<
            DX11ConstantBufferManager, ConstantBufferHandle, DX11ConstantBufferCreateDesc, DX11ConstantBufferData>
        {
        public:
            DX11ConstantBufferManager(ID3D11Device* device, ID3D11DeviceContext* context)
                : device(device), context(context) {
            }

            DX11ConstantBufferData CreateResource(const DX11ConstantBufferCreateDesc& desc) {
                DX11ConstantBufferData cb{};
                cb.name = desc.name;

                D3D11_BUFFER_DESC bd = {};
                bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                bd.ByteWidth = static_cast<UINT>(desc.size);

                HRESULT hr = device->CreateBuffer(&bd, nullptr, &cb.buffer);
                if (FAILED(hr)) {
                    assert(false && "Failed to create constant buffer");
                }

                nameToHandle[desc.name] = { (uint32_t)(slots.size()), 0 };
                return cb;
            }

            ConstantBufferHandle FindByName(const std::string& name) const {
                auto it = nameToHandle.find(name);
                if (it != nameToHandle.end()) return it->second;
                assert(false && "ConstantBuffer not found");
                return {};
            }

            ConstantBufferHandle AddWithContent(const void* data, size_t size) {
                size_t hash = HashBufferContent(data, size);
                DX11CBVCacheKey key{ hash, size };

                auto it = cbvCache.find(key);
                if (it != cbvCache.end()) {
                    AddRef(it->second);
                    return it->second;
                }

                DX11ConstantBufferCreateDesc desc{ .name = "auto_cb_" + std::to_string(hash), .size = size };
                ConstantBufferHandle handle = Add(desc);

                D3D11_MAPPED_SUBRESOURCE mapped;
                context->Map(Get(handle).buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                memcpy(mapped.pData, data, size);
                context->Unmap(Get(handle).buffer, 0);

                cbvCache[key] = handle;
                return handle;
            }

			void ScheduleDestroy(uint32_t idx, uint64_t deleteFrame) {
				slots[idx].alive = false;
				pendingDelete.push_back({ idx, deleteFrame });
			}

			void ProcessDeferredDeletes(uint64_t currentFrame) {
				auto it = pendingDelete.begin();
				while (it != pendingDelete.end()) {
					if (it->deleteSync <= currentFrame) {
						auto& data = slots[it->index].data;
						data.buffer->Release(); // バッファの参照を解放
						data.buffer = nullptr; // ポインタを無効化
						nameToHandle.erase(data.name); // 名前からハンドルを削除
						cbvCache.erase({ it->index, 0 }); // キャッシュから削除
						it = pendingDelete.erase(it);
					}
					else {
						++it;
					}
				}
			}

        private:
            ID3D11Device* device;
            ID3D11DeviceContext* context;
            std::unordered_map<std::string, ConstantBufferHandle> nameToHandle;
            std::unordered_map<DX11CBVCacheKey, ConstantBufferHandle, DX11CBVCacheKeyHash> cbvCache;

            struct PendingDelete {
                uint32_t index;
                uint64_t deleteSync;
            };
            std::vector<PendingDelete> pendingDelete;
        };
	}
}
