/*****************************************************************//**
 * @file   DX11MaterialManager.h
 * @brief DirectX 11用のマテリアルマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <bitset>
#include <unordered_map>
#include <variant>
#include "DX11ShaderManager.h"
#include "DX11TextureManager.h"
#include "DX11BufferManager.h"
#include "DX11SamplerManager.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		using ShaderResourceHandle = std::variant<std::monostate, TextureHandle, BufferHandle>;

		/**
		 * @brief DirectX 11用のマテリアル作成情報構造体
		 */
		struct MaterialCreateDesc {
			ShaderHandle shader = {};
			bool isBindVSSampler{ false }; // 頂点シェーダーでサンプラーを使用するかどうか
			std::unordered_map<uint32_t, ShaderResourceHandle> psSRV;
			std::unordered_map<uint32_t, ShaderResourceHandle> vsSRV;
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

			/**
			 * @brief バインディングを追加または上書きする
			 * @param bind 追加または上書きするバインディング情報のペア (スロット番号, バインディングオブジェクト)
			 */
			void PushOrOverwrite(std::pair<UINT, CacheType> bind) {
				if (count == 0 || valid == false)
				{
					contiguousViews.clear();
					individualViews.clear();

					valid = true;
					contiguous = true;
					minSlot = bind.first;
					count = 1;
					contiguousViews.push_back(bind.second);
					return;
				}

				if (contiguous)
				{
					auto slot = bind.first;
					auto maxSlot = minSlot + count;
					// 範囲内なら上書き
					if (minSlot <= slot && slot < maxSlot)
					{
						contiguousViews[slot - minSlot] = bind.second;
					}
					//範囲外だけど連続的(先頭)
					else if (slot == (std::max)(minSlot, (UINT)1) - 1)
					{
						contiguousViews.resize(contiguousViews.size() + 1);
						for (int i = (int)contiguousViews.size() - 1; i > 0; --i)
						{
							contiguousViews[i] = contiguousViews[i - 1];
						}
						contiguousViews[0] = bind.second;
						minSlot = slot;
					}
					//範囲外だけど連続的(末尾)
					else if (slot == maxSlot)
					{
						contiguousViews.push_back(bind.second);
						count++;
					}
					//範囲外で連続的でない
					else
					{
						contiguous = false;
						for (UINT i = minSlot; i < maxSlot; ++i)
						{
							individualViews.emplace_back(i, contiguousViews[i - minSlot]);
						}
						contiguousViews.clear();
						individualViews.emplace_back(slot, bind.second);
						minSlot = (std::min)(minSlot, slot);
						count++;
					}
				}
				else
				{
					std::bitset<128> usedSlots;
					for (auto i = 0; i < individualViews.size(); ++i)
					{
						// 既存のスロットなら上書き
						if (individualViews[i].first == bind.first)
						{
							individualViews[i].second = bind.second;
							return;
						}
						usedSlots.set(individualViews[i].first);
					}

					individualViews.emplace_back(bind.first, bind.second);
					count++;
					usedSlots.set(bind.first);
					minSlot = (std::min)(minSlot, bind.first);
					auto maxSlot = minSlot + count;
					for (UINT i = minSlot; i <= maxSlot; ++i) {
						if (!usedSlots.test(i)) {
							return; // 連続でない
						}
					}

					// 連続になった
					contiguous = true;
					contiguousViews.resize(count);
					for (const auto& [slot, view] : individualViews) {
						contiguousViews[slot - minSlot] = view;
					}
					individualViews.clear();
				}
			}
		};

		using MaterialBindingCacheSRV = MaterialBindingCache<ID3D11ShaderResourceView*>;
		using MaterialBindingCacheCBV = MaterialBindingCache<ID3D11Buffer*>;
		using MaterialBindingCacheSampler = MaterialBindingCache<ID3D11SamplerState*>;
		/**
		 * @brief DirectX 11用のマテリアルデータ構造体
		 */
		struct MaterialData {
			MaterialTemplateID templateID;
			ShaderHandle shader;
			bool isBindVSSampler{ false }; // 頂点シェーダーでサンプラーを使用するかどうか
			MaterialBindingCacheSRV psSRV, vsSRV;
			MaterialBindingCacheCBV psCBV, vsCBV; // CBVバインディングキャッシュ
			MaterialBindingCacheSampler samplerCache; // サンプラーバインディングキャッシュ
			std::vector<ShaderResourceHandle> usedSRVs; // 使用中のSRVキャッシュ
			std::vector<BufferHandle> usedCBBuffers; // 使用中のCBハンドル
			std::vector<SamplerHandle> usedSamplers; // 使用中のサンプラーハンドル
		};
		/**
		 * @brief DirectX 11用のマテリアルマネージャークラス.
		 */
		class MaterialManager : public ResourceManagerBase<MaterialManager, MaterialHandle, MaterialCreateDesc, MaterialData> {
		public:
			/**
			 * @brief コンストラクタ
			 * @param shaderMgr シェーダーマネージャークラス
			 * @param textureMgr テクスチャマネージャークラス
			 * @param cbMgr CBバッファマネージャークラス
			 * @param samplerMgr サンプラーマネージャークラス
			 */
			explicit MaterialManager(ShaderManager* shaderMgr,
				TextureManager* textureMgr,
				BufferManager* cbMgr,
				SamplerManager* samplerMgr)
				noexcept : shaderManager(shaderMgr), textureManager(textureMgr), cbManager(cbMgr), samplerManager(samplerMgr) {
			}

			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @return std::optional<MaterialHandle> 既存のマテリアルハンドル、存在しない場合は std::nullopt
			 */
			std::optional<MaterialHandle> FindExisting(const MaterialCreateDesc& desc) noexcept;
			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @param h 登録するマテリアルハンドル
			 */
			void RegisterKey(const MaterialCreateDesc& desc, MaterialHandle h);
			/**
			 * @brief ResourceManagerBase フック
			 * @param desc マテリアル作成情報
			 * @param h 登録するマテリアルハンドル
			 * @return DX11MaterialData 作成されたマテリアルデータ
			 */
			MaterialData CreateResource(const MaterialCreateDesc& desc, MaterialHandle h);
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
			static void BindMaterialPSSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache);
			static void BindMaterialVSSamplers(ID3D11DeviceContext* ctx, const MaterialBindingCacheSampler& cache);

			static ID3D11ShaderResourceView* ResolveSRV(
				const ShaderResourceHandle& h,
				DX11::TextureManager& texMgr,
				DX11::BufferManager& bufMgr
			);

			static void AddRefSRV(
				const ShaderResourceHandle& h,
				DX11::TextureManager& texMgr,
				DX11::BufferManager& bufMgr
			);

			static void ReleaseSRV(
				const ShaderResourceHandle& h,
				DX11::TextureManager& texMgr,
				DX11::BufferManager& bufMgr,
				bool del
			);

			static uint32_t ResolveHandleIndex(const ShaderResourceHandle& h) {
				uint32_t index = 0;

				std::visit([&](auto&& v) {
					using T = std::decay_t<decltype(v)>;
					if constexpr (std::is_same_v<T, TextureHandle>) index = v.index;
					else if constexpr (std::is_same_v<T, BufferHandle>) index = v.index;
					}, h);

				return index;
			}

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
			ShaderManager* shaderManager;
			TextureManager* textureManager;
			BufferManager* cbManager;
			SamplerManager* samplerManager;

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

			static MaterialKey MakeKey(const MaterialCreateDesc& desc);

			std::unordered_map<MaterialKey, MaterialHandle, MaterialKeyHash> matCache;
			std::unordered_map<uint32_t, MaterialKey> handleToKey;
		};
	}
}
