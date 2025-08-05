#include "Graphics/DX11/DX11MaterialManager.h"

#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
		DX11MaterialData DX11MaterialManager::CreateResource(const DX11MaterialCreateDesc& desc)
		{
			auto& shader = shaderManager->Get(desc.shader);

			DX11MaterialData mat{};
			mat.templateID = shader.templateID;
			mat.shader = desc.shader;
			mat.usedTextures.reserve(desc.srvMap.size());
			for (const auto& [slot, texHandle] : desc.srvMap) {
				mat.usedTextures.push_back(texHandle);
			}

			// === リフレクション情報取得 ===
			const auto& bindings = shader.bindings;

			std::unordered_map<UINT, ID3D11ShaderResourceView*> srvMap;
			for (const auto& [slot, texHandle] : desc.srvMap) {
				const auto& texData = textureManager->Get(texHandle);
				srvMap[slot] = texData.srv.Get();
				mat.usedTextures.push_back(texHandle);
				textureManager->AddRef(texHandle); // 所有権追跡開始
			}

			std::unordered_map<UINT, ID3D11Buffer*> cbvMap;
			for (const auto& [slot, cbHandle] : desc.cbvMap) {
				const auto& cbData = cbManager->Get(cbHandle);
				cbvMap[slot] = cbData.buffer;
				mat.usedCBBuffers.push_back(cbHandle);
			}

			std::unordered_map<UINT, ID3D11SamplerState*> samplerMap;
			for (const auto& [slot, samplerHandle] : desc.samplerMap) {
				const auto& samplerData = samplerManager->Get(samplerHandle);
				samplerMap[slot] = samplerData.state;
				mat.usedSamplers.push_back(samplerHandle);
				samplerManager->AddRef(samplerHandle); // 所有権追跡開始
			}

			// === キャッシュ構築 ===
			mat.textureCache = BuildBindingCacheSRV(bindings, srvMap);
			mat.textureCache.valid = mat.textureCache.minSlot != UINT_MAX; // キャッシュが有効

			mat.cbvCache = BuildBindingCacheCBV(bindings, cbvMap);
			mat.cbvCache.valid = mat.cbvCache.minSlot != UINT_MAX; // CBVキャッシュが有効

			mat.samplerCache = BuildBindingCacheSampler(bindings, samplerMap);
			mat.samplerCache.valid = mat.samplerCache.minSlot != UINT_MAX; // サンプラキャッシュが有効

			return mat;
		}

		void DX11MaterialManager::ScheduleDestroy(uint32_t idx, uint64_t deleteFrame)
		{
			slots[idx].alive = false;
			pendingDelete.push_back({ idx, deleteFrame });
		}
		void DX11MaterialManager::ProcessDeferredDeletes(uint64_t currentFrame)
		{
			auto it = pendingDelete.begin();
			while (it != pendingDelete.end()) {
				if (it->deleteSync <= currentFrame) {
					auto& data = slots[it->index].data;
					data.textureCache.valid = false; // キャッシュを無効化
					data.cbvCache.valid = false; // CBVキャッシュを無効化
					data.samplerCache.valid = false; // サンプラキャッシュを無効化

					uint64_t deleteSync = currentFrame + RENDER_QUEUE_BUFFER_COUNT;
					for (auto& texHandle : data.usedTextures) {
						textureManager->Release(texHandle, deleteSync); // テクスチャの参照を解放
					}
					for (auto& cbHandle : data.usedCBBuffers) {
						cbManager->Release(cbHandle, deleteSync); // CBの参照を解放
					}
					for (auto& samplerHandle : data.usedSamplers) {
						samplerManager->Release(samplerHandle, deleteSync); // サンプラの参照を解放
					}

					freeList.push_back(it->index);
					it = pendingDelete.erase(it);
				}
				else {
					++it;
				}
			}
		}
		void DX11MaterialManager::BindMaterialSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache)
		{
			if (!cache.valid) {
				LOG_ERROR("Attempted to bind invalid material SRV cache.");
				//assert(false && "Invalid material SRV cache");
				return; // キャッシュが無効なら何もしない
			}

			if (cache.contiguous) {
				ctx->PSSetShaderResources(cache.minSlot, cache.count, cache.contiguousViews.data());
			}
			else {
				for (auto& [slot, srv] : cache.individualViews) {
					ctx->PSSetShaderResources(slot, 1, &srv);
				}
			}
		}
		void DX11MaterialManager::BindMaterialCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache)
		{
			if (!cache.valid) {
				LOG_ERROR("Attempted to bind invalid material CBV cache.");
				//assert(false && "Invalid material CBV cache");
				return; // キャッシュが無効なら何もしない
			}
			if (cache.contiguous) {
				ctx->VSSetConstantBuffers(cache.minSlot, cache.count, cache.contiguousViews.data());
			}
			else {
				for (auto& [slot, cbv] : cache.individualViews) {
					ctx->VSSetConstantBuffers(slot, 1, &cbv);
				}
			}
		}
		void DX11MaterialManager::BindMaterialSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache)
		{
			if (!cache.valid) {
				LOG_ERROR("Attempted to bind invalid material sampler cache.");
				//assert(false && "Invalid material sampler cache");
				return; // キャッシュが無効なら何もしない
			}
			if (cache.contiguous) {
				ctx->PSSetSamplers(cache.minSlot, cache.count, cache.contiguousViews.data());
			}
			else {
				for (auto& [slot, sampler] : cache.individualViews) {
					ctx->PSSetSamplers(slot, 1, &sampler);
				}
			}
		}
		MaterialBindingCacheSRV DX11MaterialManager::BuildBindingCacheSRV(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11ShaderResourceView*>& srvMap)
		{
			MaterialBindingCacheSRV result;

			UINT minSlot = UINT_MAX;
			UINT maxSlot = 0;
			std::bitset<128> usedSlots;

			for (const auto& b : bindings) {
				if (b.type != D3D_SIT_TEXTURE) continue;
				if (auto it = srvMap.find(b.bindPoint); it != srvMap.end()) {
					usedSlots.set(b.bindPoint);
					minSlot = (std::min)(minSlot, b.bindPoint);
					maxSlot = (std::max)(maxSlot, b.bindPoint);
				}
			}

			result.minSlot = minSlot;
			result.count = (minSlot <= maxSlot) ? (maxSlot - minSlot + 1) : 0;
			result.contiguous = true;

			for (UINT i = minSlot; i <= maxSlot; ++i) {
				if (!usedSlots.test(i)) {
					result.contiguous = false;
					break;
				}
			}

			if (result.contiguous) {
				result.contiguousViews.resize(result.count, nullptr);
				for (const auto& b : bindings) {
					if (b.type != D3D_SIT_TEXTURE) continue;
					auto it = srvMap.find(b.bindPoint);
					if (it != srvMap.end()) {
						result.contiguousViews[b.bindPoint - minSlot] = it->second;
					}
				}
			}
			else {
				for (const auto& b : bindings) {
					if (b.type != D3D_SIT_TEXTURE) continue;
					auto it = srvMap.find(b.bindPoint);
					if (it != srvMap.end()) {
						result.individualViews.emplace_back(b.bindPoint, it->second);
					}
				}
			}

			return result;
		}
		MaterialBindingCacheCBV DX11MaterialManager::BuildBindingCacheCBV(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11Buffer*>& cbvMap)
		{
			MaterialBindingCacheCBV result;

			UINT minSlot = UINT_MAX;
			UINT maxSlot = 0;
			std::bitset<128> usedSlots;

			for (const auto& b : bindings) {
				if (b.type != D3D_SIT_CBUFFER) continue;
				if (auto it = cbvMap.find(b.bindPoint); it != cbvMap.end()) {
					usedSlots.set(b.bindPoint);
					minSlot = (std::min)(minSlot, b.bindPoint);
					maxSlot = (std::max)(maxSlot, b.bindPoint);
				}
			}

			result.minSlot = minSlot;
			result.count = (minSlot <= maxSlot) ? (maxSlot - minSlot + 1) : 0;
			result.contiguous = true;

			for (UINT i = minSlot; i <= maxSlot; ++i) {
				if (!usedSlots.test(i)) {
					result.contiguous = false;
					break;
				}
			}

			if (result.contiguous) {
				result.contiguousViews.resize(result.count);
				for (const auto& [slot, handle] : cbvMap) {
					if (slot >= minSlot && slot <= maxSlot)
						result.contiguousViews[slot - minSlot] = handle;
				}
			}
			else {
				for (const auto& [slot, handle] : cbvMap) {
					result.individualViews.emplace_back(slot, handle);
				}
			}

			return result;
		}
		MaterialBindingCacheSampler DX11MaterialManager::BuildBindingCacheSampler(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11SamplerState*>& samplerMap)
		{
			MaterialBindingCacheSampler result;

			UINT minSlot = UINT_MAX;
			UINT maxSlot = 0;
			std::bitset<128> usedSlots;

			for (const auto& b : bindings) {
				if (b.type != D3D_SIT_SAMPLER) continue;
				if (auto it = samplerMap.find(b.bindPoint); it != samplerMap.end()) {
					usedSlots.set(b.bindPoint);
					minSlot = (std::min)(minSlot, b.bindPoint);
					maxSlot = (std::max)(maxSlot, b.bindPoint);
				}
			}

			result.minSlot = minSlot;
			result.count = (minSlot <= maxSlot) ? (maxSlot - minSlot + 1) : 0;
			result.contiguous = true;

			for (UINT i = minSlot; i <= maxSlot; ++i) {
				if (!usedSlots.test(i)) {
					result.contiguous = false;
					break;
				}
			}

			if (result.contiguous) {
				result.contiguousViews.resize(result.count);
				for (const auto& [slot, handle] : samplerMap) {
					if (slot >= minSlot && slot <= maxSlot)
						result.contiguousViews[slot - minSlot] = handle;
				}
			}
			else {
				for (const auto& [slot, handle] : samplerMap) {
					result.individualViews.emplace_back(slot, handle);
				}
			}

			return result;
		}
	}
}
