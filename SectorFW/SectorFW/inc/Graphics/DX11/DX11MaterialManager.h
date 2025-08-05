#pragma once

#include <bitset>
#include <unordered_map>
#include "DX11ShaderManager.h"
#include "DX11TextureManager.h"
#include "DX11ConstantBufferManager.h"
#include "DX11SamplerManager.h"

namespace SectorFW
{
	namespace Graphics
	{
		struct DX11MaterialCreateDesc {
			ShaderHandle shader;
			std::unordered_map<UINT, TextureHandle> srvMap;
			std::unordered_map<UINT, ConstantBufferHandle> cbvMap; // CBVバインディング
			std::unordered_map<UINT, SamplerHandle> samplerMap; // サンプラーバインディング
		};

		template<typename CacheType>
		struct MaterialBindingCache {
			bool valid = false; // キャッシュが有効かどうか
			bool contiguous = false;
			UINT minSlot = 0;
			UINT count = 0;
			std::vector<CacheType> contiguousViews;
			std::vector<std::pair<UINT, CacheType>> individualViews;
		};

		using MaterialBindingCacheSRV = MaterialBindingCache<ID3D11ShaderResourceView*>;
		using MaterialBindingCacheCBV = MaterialBindingCache<ID3D11Buffer*>;
		using MaterialBindingCacheSampler = MaterialBindingCache<ID3D11SamplerState*>;

		struct DX11MaterialData {
			MaterialTemplateID templateID;
			ShaderHandle shader;
			MaterialBindingCacheSRV textureCache;
			MaterialBindingCacheCBV cbvCache; // CBVバインディングキャッシュ
			MaterialBindingCacheSampler samplerCache; // サンプラーバインディングキャッシュ
			std::vector<TextureHandle> usedTextures; // テクスチャハンドルのキャッシュ
			std::vector<ConstantBufferHandle> usedCBBuffers; // 使用中のCBハンドル
			std::vector<SamplerHandle> usedSamplers; // 使用中のサンプラーハンドル
		};

		class DX11MaterialManager : public ResourceManagerBase<DX11MaterialManager, MaterialHandle, DX11MaterialCreateDesc, DX11MaterialData> {
		public:
			explicit DX11MaterialManager(DX11ShaderManager* shaderMgr,
				DX11TextureManager* textureMgr,
				DX11ConstantBufferManager* cbMgr,
				DX11SamplerManager* samplerMgr)
				noexcept : shaderManager(shaderMgr), textureManager(textureMgr), cbManager(cbMgr), samplerManager(samplerMgr) {}

			DX11MaterialData CreateResource(const DX11MaterialCreateDesc& desc);

			void ScheduleDestroy(uint32_t idx, uint64_t deleteFrame);

			void ProcessDeferredDeletes(uint64_t currentFrame);

			static void BindMaterialSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache);
			static void BindMaterialCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache);
			static void BindMaterialSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache);
		private:
			MaterialBindingCacheSRV BuildBindingCacheSRV(
				const std::vector<ShaderResourceBinding>& bindings,
				const std::unordered_map<UINT, ID3D11ShaderResourceView*>& srvMap);

			MaterialBindingCacheCBV BuildBindingCacheCBV(
				const std::vector<ShaderResourceBinding>& bindings,
				const std::unordered_map<UINT, ID3D11Buffer*>& cbvMap);

			MaterialBindingCacheSampler BuildBindingCacheSampler(
				const std::vector<ShaderResourceBinding>& bindings,
				const std::unordered_map<UINT, ID3D11SamplerState*>& samplerMap);

		private:
			struct PendingDelete { uint32_t index; uint64_t deleteSync; };
			std::vector<PendingDelete> pendingDelete;
			DX11ShaderManager* shaderManager;
			DX11TextureManager* textureManager;
			DX11ConstantBufferManager* cbManager;
			DX11SamplerManager* samplerManager;
		};

		struct DX11MaterialAutoBindContext {
			DX11ConstantBufferManager* cbManager = nullptr;
			DX11SamplerManager* samplerManager = nullptr;
			DX11TextureManager* texManager = nullptr;
		};
	}
}
