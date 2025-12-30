#include "Graphics/DX11/DX11MaterialManager.h"

#include "Debug/logger.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		// --- 追加: Key 作成ヘルパ ---
		MaterialManager::MaterialKey MaterialManager::MakeKey(const MaterialCreateDesc& desc) {
			MaterialManager::MaterialKey k;
			k.shaderIndex = desc.shader.index;

			k.psSrvs.reserve(desc.psSRV.size());
			for (auto& [slot, h] : desc.psSRV)     k.psSrvs.emplace_back(slot, h.index);

			k.vsSrvs.reserve(desc.vsSRV.size());
			for (auto& [slot, h] : desc.vsSRV)     k.vsSrvs.emplace_back(slot, h.index);

			k.psCbvs.reserve(desc.psCBV.size());
			for (auto& [slot, h] : desc.psCBV)     k.psCbvs.emplace_back(slot, h.index);

			k.vsCbvs.reserve(desc.vsCBV.size());
			for (auto& [slot, h] : desc.vsCBV)     k.vsCbvs.emplace_back(slot, h.index);

			k.samplers.reserve(desc.samplerMap.size());
			for (auto& [slot, h] : desc.samplerMap) k.samplers.emplace_back(slot, h.index);

			auto bySlot = [](auto& a, auto& b) { return a.first < b.first; };
			std::sort(k.psSrvs.begin(), k.psSrvs.end(), bySlot);
			std::sort(k.vsSrvs.begin(), k.vsSrvs.end(), bySlot);
			std::sort(k.psCbvs.begin(), k.psCbvs.end(), bySlot);
			std::sort(k.vsCbvs.begin(), k.vsCbvs.end(), bySlot);
			std::sort(k.samplers.begin(), k.samplers.end(), bySlot);
			return k;
		}

		std::optional<MaterialHandle>
			MaterialManager::FindExisting(const MaterialCreateDesc& desc) noexcept {
			auto key = MakeKey(desc);
			if (auto it = matCache.find(key); it != matCache.end()) {
				return it->second;
			}
			return std::nullopt;
		}
		void MaterialManager::RegisterKey(const MaterialCreateDesc& desc, MaterialHandle h) {
			auto key = MakeKey(desc);
			matCache.emplace(key, h);
			handleToKey.emplace(h.index, std::move(key));
		}

		MaterialData MaterialManager::CreateResource(const MaterialCreateDesc& desc, MaterialHandle h)
		{
			MaterialData mat{};
			mat.isBindVSSampler = desc.isBindVSSampler;

			std::unordered_map<UINT, ID3D11ShaderResourceView*> psSRVMap;
			for (const auto& [slot, texHandle] : desc.psSRV) {
				auto texData = textureManager->Get(texHandle);
				psSRVMap[slot] = texData.ref().srv.Get();
				mat.usedTextures.push_back(texHandle);
				textureManager->AddRef(texHandle); // 所有権追跡開始
			}

			std::unordered_map<UINT, ID3D11ShaderResourceView*> vsSRVMap;
			for (const auto& [slot, texHandle] : desc.vsSRV) {
				auto texData = textureManager->Get(texHandle);
				vsSRVMap[slot] = texData.ref().srv.Get();
				mat.usedTextures.push_back(texHandle);
				textureManager->AddRef(texHandle); // 所有権追跡開始
			}

			std::unordered_map<UINT, ID3D11Buffer*> psCBVMap;
			for (const auto& [slot, cbHandle] : desc.psCBV) {
				auto cbData = cbManager->Get(cbHandle);
				psCBVMap[slot] = cbData.ref().buffer.Get();
				mat.usedCBBuffers.push_back(cbHandle);
				cbManager->AddRef(cbHandle);  // 所有権追跡開始
			}

			std::unordered_map<UINT, ID3D11Buffer*> vsCBVMap;
			for (const auto& [slot, cbHandle] : desc.vsCBV) {
				auto cbData = cbManager->Get(cbHandle);
				vsCBVMap[slot] = cbData.ref().buffer.Get();
				mat.usedCBBuffers.push_back(cbHandle);
				cbManager->AddRef(cbHandle);  // 所有権追跡開始
			}

			std::unordered_map<UINT, ID3D11SamplerState*> samplerMap;
			for (const auto& [slot, samplerHandle] : desc.samplerMap) {
				auto samplerData = samplerManager->Get(samplerHandle);
				samplerMap[slot] = samplerData.ref().state.Get();
				mat.usedSamplers.push_back(samplerHandle);
				samplerManager->AddRef(samplerHandle); // 所有権追跡開始
			}

			auto shader = shaderManager->Get(desc.shader);
			mat.templateID = shader.ref().templateID;
			mat.shader = desc.shader;
			mat.usedTextures.reserve(desc.psSRV.size());

			// === リフレクション情報取得 ===
			const auto& psBindings = shader.ref().psBindings;
			const auto& vsBindings = shader.ref().vsBindings;

			// === キャッシュ構築 ===
			mat.psSRV = BuildBindingCacheSRV(psBindings, psSRVMap);
			mat.psSRV.valid = mat.psSRV.minSlot != UINT_MAX; // キャッシュが有効

			mat.vsSRV = BuildBindingCacheSRV(vsBindings, vsSRVMap);
			mat.vsSRV.valid = mat.vsSRV.minSlot != UINT_MAX; // キャッシュが有効

			mat.psCBV = BuildBindingCacheCBV(psBindings, psCBVMap);
			mat.psCBV.valid = mat.psCBV.minSlot != UINT_MAX; // CBVキャッシュが有効

			mat.vsCBV = BuildBindingCacheCBV(vsBindings, vsCBVMap);
			mat.vsCBV.valid = mat.vsCBV.minSlot != UINT_MAX; // CBVキャッシュが有効

			mat.samplerCache = BuildBindingCacheSampler(psBindings, samplerMap);
			mat.samplerCache.valid = mat.samplerCache.minSlot != UINT_MAX; // サンプラキャッシュが有効

			return mat;
		}

		void MaterialManager::RemoveFromCaches(uint32_t idx)
		{
			if (auto k = handleToKey.find(idx); k != handleToKey.end()) {
				matCache.erase(k->second);
				handleToKey.erase(k);
			}
		}
		void MaterialManager::DestroyResource(uint32_t idx, uint64_t currentFrame)
		{
			auto& m = slots[idx].data;
			m.psSRV.valid = false;
			m.vsSRV.valid = false;
			m.psCBV.valid = false;
			m.vsCBV.valid = false;
			m.samplerCache.valid = false;

			// 子の参照を連鎖解放（1～2フレ先に遅延）
			const uint64_t del = currentFrame + RENDER_BUFFER_COUNT;
			for (auto& th : m.usedTextures)  textureManager->Release(th, del);
			for (auto& cb : m.usedCBBuffers) cbManager->Release(cb, del);
			for (auto& sp : m.usedSamplers)  samplerManager->Release(sp, del);
		}

		void MaterialManager::BindMaterialPSSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->PSSetShaderResources(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, srv] : cache.individualViews) ctx->PSSetShaderResources(slot, 1, &srv);
		}
		void MaterialManager::BindMaterialVSSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->VSSetShaderResources(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, srv] : cache.individualViews) ctx->VSSetShaderResources(slot, 1, &srv);
		}
		void MaterialManager::BindMaterialPSCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->PSSetConstantBuffers(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, cbv] : cache.individualViews) ctx->PSSetConstantBuffers(slot, 1, &cbv);
		}
		void MaterialManager::BindMaterialVSCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->VSSetConstantBuffers(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, cbv] : cache.individualViews) ctx->VSSetConstantBuffers(slot, 1, &cbv);
		}
		void MaterialManager::BindMaterialPSSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->PSSetSamplers(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, sampler] : cache.individualViews) ctx->PSSetSamplers(slot, 1, &sampler);
		}
		void MaterialManager::BindMaterialVSSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache)
		{
			if (!cache.valid) return; // キャッシュが無効なら何もしない
			if (cache.contiguous) ctx->VSSetSamplers(cache.minSlot, cache.count, cache.contiguousViews.data());
			else for (auto& [slot, sampler] : cache.individualViews) ctx->VSSetSamplers(slot, 1, &sampler);
		}
		MaterialBindingCacheSRV MaterialManager::BuildBindingCacheSRV(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11ShaderResourceView*>& srvMap)
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
		MaterialBindingCacheCBV MaterialManager::BuildBindingCacheCBV(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11Buffer*>& cbvMap)
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
		MaterialBindingCacheSampler MaterialManager::BuildBindingCacheSampler(const std::vector<ShaderResourceBinding>& bindings, const std::unordered_map<UINT, ID3D11SamplerState*>& samplerMap)
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