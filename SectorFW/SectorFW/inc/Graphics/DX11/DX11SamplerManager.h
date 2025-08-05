#pragma once

#include "_dx11inc.h"
#include "../ResouceManagerBase.hpp"

#include <unordered_map>
#include <string>

inline bool operator==(const D3D11_SAMPLER_DESC& a, const D3D11_SAMPLER_DESC& b) {
    return memcmp(&a, &b, sizeof(D3D11_SAMPLER_DESC)) == 0;
}

namespace SectorFW
{
	namespace Graphics
	{
        struct DX11SamplerCreateDesc {
            std::string name;
            D3D11_SAMPLER_DESC desc;
        };

        struct DX11SamplerData {
            ID3D11SamplerState* state;
            std::string name;
        };

        struct DX11SamplerDescHash {
            std::size_t operator()(const D3D11_SAMPLER_DESC& desc) const {
                return HashBufferContent(&desc, sizeof(desc));
            }
        };

        class DX11SamplerManager : public ResourceManagerBase<
            DX11SamplerManager, SamplerHandle, DX11SamplerCreateDesc, DX11SamplerData>
        {
        public:
            DX11SamplerManager(ID3D11Device* device) : device(device) {}

            DX11SamplerData CreateResource(const DX11SamplerCreateDesc& desc) {
                DX11SamplerData data{};
                data.name = desc.name;

                HRESULT hr = device->CreateSamplerState(&desc.desc, &data.state);
                if (FAILED(hr)) {
                    assert(false && "Failed to create sampler");
                }

                nameToHandle[desc.name] = { (uint32_t)(slots.size()), 0 };
                return data;
            }

            SamplerHandle FindByName(const std::string& name) const {
                auto it = nameToHandle.find(name);
                if (it != nameToHandle.end()) return it->second;
                assert(false && "Sampler not found");
                return {};
            }

            SamplerHandle AddWithDesc(const D3D11_SAMPLER_DESC& desc) {
                auto it = samplerCache.find(desc);
                if (it != samplerCache.end()) {
                    AddRef(it->second);
                    return it->second;
                }

                SamplerHandle handle = Add({ "generated", desc });
                samplerCache[desc] = handle;
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
						data.state->Release(); // サンプラーの参照を解放
						data.state = nullptr; // ポインタを無効化
						freeList.push_back(it->index);
						it = pendingDelete.erase(it);
					}
					else {
						++it;
					}
				}
            }

        private:
            ID3D11Device* device;
            std::unordered_map<std::string, SamplerHandle> nameToHandle;
            std::unordered_map<D3D11_SAMPLER_DESC, SamplerHandle, DX11SamplerDescHash> samplerCache;

            struct PendingDelete {
                uint32_t index;
                uint64_t deleteSync;
            };
            std::vector<PendingDelete> pendingDelete;
        };
	}
}
