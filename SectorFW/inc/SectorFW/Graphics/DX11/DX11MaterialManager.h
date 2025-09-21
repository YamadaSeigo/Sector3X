/*****************************************************************//**
 * @file   DX11MaterialManager.h
 * @brief DirectX 11用のマテリアルマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <bitset>
#include <unordered_map>
#include "DX11ShaderManager.h"
#include "DX11TextureManager.h"
#include "DX11BufferManager.h"
#include "DX11SamplerManager.h"

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX 11用のマテリアル作成情報構造体
		 */
		struct DX11MaterialCreateDesc {
			ShaderHandle shader;
			std::unordered_map<UINT, TextureHandle> psSRV;
			std::unordered_map<UINT, TextureHandle> vsSRV;
			std::unordered_map<UINT, BufferHandle> psCBV; // CBVバインディング
			std::unordered_map<UINT, BufferHandle> vsCBV; // CBVバインディング
			std::unordered_map<UINT, SamplerHandle> samplerMap; // サンプラーバインディング
		};
		/**
		 * @brief マテリアルバインディングキャッシュ構造体
		 */
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
		/**
		 * @brief DirectX 11用のマテリアルデータ構造体
		 */
		struct DX11MaterialData {
			MaterialTemplateID templateID;
			ShaderHandle shader;
			MaterialBindingCacheSRV psSRV, vsSRV;
			MaterialBindingCacheCBV psCBV, vsCBV; // CBVバインディングキャッシュ
			MaterialBindingCacheSampler samplerCache; // サンプラーバインディングキャッシュ
			std::vector<TextureHandle> usedTextures; // テクスチャハンドルのキャッシュ
			std::vector<BufferHandle> usedCBBuffers; // 使用中のCBハンドル
			std::vector<SamplerHandle> usedSamplers; // 使用中のサンプラーハンドル
		};
		/**
		 * @brief DirectX 11用のマテリアルマネージャークラス.
		 */
		class DX11MaterialManager : public ResourceManagerBase<DX11MaterialManager, MaterialHandle, DX11MaterialCreateDesc, DX11MaterialData> {
		public:
			/**
			 * @brief コンストラクタ
			 * @param shaderMgr シェーダーマネージャークラス
			 * @param textureMgr テクスチャマネージャークラス
			 * @param cbMgr CBバッファマネージャークラス
			 * @param samplerMgr サンプラーマネージャークラス
			 */
			explicit DX11MaterialManager(DX11ShaderManager* shaderMgr,
				DX11TextureManager* textureMgr,
				DX11BufferManager* cbMgr,
				DX11SamplerManager* samplerMgr)
				noexcept : shaderManager(shaderMgr), textureManager(textureMgr), cbManager(cbMgr), samplerManager(samplerMgr) {
			}

			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @return std::optional<MaterialHandle> 既存のマテリアルハンドル、存在しない場合は std::nullopt
			 */
			std::optional<MaterialHandle> FindExisting(const DX11MaterialCreateDesc& desc) noexcept;
			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @param h 登録するマテリアルハンドル
			 */
			void RegisterKey(const DX11MaterialCreateDesc& desc, MaterialHandle h);
			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @param h 登録するマテリアルハンドル
			 * @return DX11MaterialData 作成されたマテリアルデータ
			 */
			DX11MaterialData CreateResource(const DX11MaterialCreateDesc& desc, MaterialHandle h);
			/**
			 * @brief 指定したインデックスのマテリアルをキャッシュから削除する関数
			 * @param idx 削除するマテリアルのインデックス
			 */
			void RemoveFromCaches(uint32_t idx);
			/**
			 * @brief 指定したインデックスのマテリアルリソースを破棄する関数
			 * @param idx 破棄するマテリアルのインデックス
			 * @param currentFrame 現在のフレーム番号（遅延破棄用）
			 */
			void DestroyResource(uint32_t idx, uint64_t currentFrame);
			/**
			 * @brief マテリアルのシェーダーリソースビューをバインドする関数
			 * @param ctx デバイスコンテキスト
			 * @param cache バインディングキャッシュ
			 */
			static void BindMaterialPSSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache);
			static void BindMaterialVSSRVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheSRV& cache);
			/**
			 * @brief マテリアルの定数バッファビューをバインドする関数
			 * @param ctx デバイスコンテキスト
			 * @param cache バインディングキャッシュ
			 */
			static void BindMaterialPSCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache);
			static void BindMaterialVSCBVs(ID3D11DeviceContext* ctx, const MaterialBindingCacheCBV& cache);
			/**
			 * @brief マテリアルのサンプラーをバインドする関数
			 * @param ctx デバイスコンテキスト
			 * @param cache バインディングキャッシュ
			 */
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
			DX11ShaderManager* shaderManager;
			DX11TextureManager* textureManager;
			DX11BufferManager* cbManager;
			DX11SamplerManager* samplerManager;

			// ==== マテリアルキー（不変の組をソートしてハッシュ化）====
			struct MaterialKey {
				uint32_t shaderIndex{};
				std::vector<std::pair<UINT, uint32_t>> psSrvs;
				std::vector<std::pair<UINT, uint32_t>> vsSrvs;
				std::vector<std::pair<UINT, uint32_t>> psCbvs;
				std::vector<std::pair<UINT, uint32_t>> vsCbvs;
				std::vector<std::pair<UINT, uint32_t>> samplers;
				bool operator==(const MaterialKey&) const = default;
			};
			struct MaterialKeyHash {
				size_t operator()(MaterialKey const& k) const noexcept {
					auto hc = [](size_t& seed, size_t v) {
						seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
						};
					size_t h = 0;
					hc(h, k.shaderIndex);
					for (auto& p : k.psSrvs) { hc(h, (size_t)p.first); hc(h, (size_t)p.second); }
					for (auto& p : k.psCbvs) { hc(h, (size_t)p.first); hc(h, (size_t)p.second); }
					for (auto& p : k.samplers) { hc(h, (size_t)p.first); hc(h, (size_t)p.second); }
					return h;
				}
			};

			static MaterialKey MakeKey(const DX11MaterialCreateDesc& desc);

			std::unordered_map<MaterialKey, MaterialHandle, MaterialKeyHash> matCache;
			std::unordered_map<uint32_t, MaterialKey> handleToKey;
		};
	}
}
