#pragma once

#include "_dx11inc.h"
#include "Util/ResouceManagerBase.hpp"

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
			ComPtr<ID3D11SamplerState> state;
			std::string_view name;
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

			std::optional<SamplerHandle> FindExisting(const DX11SamplerCreateDesc& desc) {
				if (auto it = nameToHandle.find(desc.name); it != nameToHandle.end())
					return it->second;
				return std::nullopt;
			}
			void RegisterKey(const DX11SamplerCreateDesc& desc, SamplerHandle h) {
				nameToHandle.emplace(desc.name, h);
			}

			DX11SamplerData CreateResource(const DX11SamplerCreateDesc& desc, SamplerHandle h) {
				DX11SamplerData data{};

				HRESULT hr = device->CreateSamplerState(&desc.desc, &data.state);
				if (FAILED(hr)) {
					assert(false && "Failed to create sampler");
				}

				auto node = nameToHandle.emplace(std::make_pair(desc.name, h));
				data.name = node.first->first;

				return data;
			}

			static void NormalizeDesc(D3D11_SAMPLER_DESC& d)
			{
				// 既定を軽く穴埋め（必要に応じて調整）
				if (d.AddressU == 0) d.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
				if (d.AddressV == 0) d.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
				if (d.AddressW == 0) d.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

				// 境界色未設定なら 0 で初期化
				if (d.AddressU == D3D11_TEXTURE_ADDRESS_BORDER ||
					d.AddressV == D3D11_TEXTURE_ADDRESS_BORDER ||
					d.AddressW == D3D11_TEXTURE_ADDRESS_BORDER) {
					// どれかが BORDER なら念のため全部埋める
					for (int i = 0; i < 4; ++i) d.BorderColor[i] = 0.0f;
				}

				if (d.ComparisonFunc == D3D11_COMPARISON_NEVER &&
					(d.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR ||
						d.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT ||
						d.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)) {
					// 比較フィルタなのに ComparisonFunc が未設定っぽい場合、LESS_EQUALに
					d.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
				}

				if ((d.Filter == D3D11_FILTER_ANISOTROPIC ||
					d.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC) &&
					d.MaxAnisotropy == 0) {
					d.MaxAnisotropy = 8;
				}
			}

			// 16進の短い文字列を作るヘルパ（任意）
			static std::string Hex64(uint64_t v) {
				char buf[17];
				snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
				return std::string(buf);
			}

			SamplerHandle AddWithDesc(const D3D11_SAMPLER_DESC& in)
			{
				D3D11_SAMPLER_DESC desc = in;
				NormalizeDesc(desc);

				// 1) キャッシュヒットなら再利用（+1）
				{
					std::scoped_lock lock(cacheMutex);
					auto it = samplerCache.find(desc);
					if (it != samplerCache.end()) {
						AddRef(it->second);
						return it->second;
					}
				}

				// 2) 新規作成（名前をハッシュから生成）
				const size_t h = HashBufferContent(&desc, sizeof(desc));
				std::string genName = "samp_" + Hex64(static_cast<uint64_t>(h));

				SamplerHandle handle;
				Add({ genName, desc }, handle); // Add は ref=1 を返す契約

				// 3) キャッシュ登録（逆引きも）
				{
					std::scoped_lock lock(cacheMutex);
					samplerCache.emplace(desc, handle);
					handleToDesc.emplace(handle.index, desc);
				}

				return handle;
			}

			SamplerHandle FindByName(const std::string& name) const {
				auto it = nameToHandle.find(name);
				if (it != nameToHandle.end()) return it->second;
				assert(false && "Sampler not found");
				return {};
			}

			void RemoveFromCaches(uint32_t idx) {
				auto& d = slots[idx].data;
				if (!std::string(d.name).empty()) nameToHandle.erase(std::string(d.name));
				if (auto k = handleToDesc.find(idx); k != handleToDesc.end()) {
					samplerCache.erase(k->second);
					handleToDesc.erase(k);
				}
			}
			void DestroyResource(uint32_t idx, uint64_t /*currentFrame*/) {
				slots[idx].data.state.Reset();
			}

		private:
			ID3D11Device* device;
			std::unordered_map<D3D11_SAMPLER_DESC, SamplerHandle, DX11SamplerDescHash> samplerCache;
			std::unordered_map<uint32_t, D3D11_SAMPLER_DESC> handleToDesc;

			std::unordered_map<std::string, SamplerHandle> nameToHandle;

			std::mutex cacheMutex; // samplerCache / handleToDesc を守る
		};
	}
}
