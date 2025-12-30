/*****************************************************************//**
 * @file   DX11BufferManager.h
 * @brief DirectX 11のバッファマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "_dx11inc.h"
#include "../../Util/ResouceManagerBase.hpp"
#include <unordered_map>
#include <string>
#include <typeindex>

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief DirectX 11のバッファを作成するための構造体
		 */
		struct BufferCreateDesc {
			std::string name;
			uint32_t size = {};
			uint32_t structureByteStride = 0; // StructuredBuffer 用（CBV では無視される）
			const void* initialData = nullptr; // 初期データ（nullptr ならゼロクリア）
			D3D11_USAGE usage = D3D11_USAGE_DYNAMIC;
			D3D11_BIND_FLAG bindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_RESOURCE_MISC_FLAG miscFlags = {};
			D3D11_CPU_ACCESS_FLAG cpuAccessFlags = D3D11_CPU_ACCESS_WRITE; // D3D11_USAGE_STAGING用
		};
		/**
		 * @brief DirectX 11のバッファデータを格納する構造体
		 */
		struct BufferData {
			ComPtr<ID3D11Buffer> buffer;
			std::string_view name;
		};
		/**
		 * @brief DirectX 11のバッファのハッシュ構造体
		 */
		struct BufferCacheKey {
			size_t hash;
			size_t size;

			bool operator==(const BufferCacheKey& other) const {
				return hash == other.hash && size == other.size;
			}
		};
		/**
		 * @brief DirectX 11のバッファの内容をハッシュ化するための関数
		 */
		struct BufferCacheKeyHash {
			std::size_t operator()(const BufferCacheKey& k) const {
				return std::hash<size_t>()(k.hash) ^ std::hash<size_t>()(k.size);
			}
		};
		/**
		 * @brief DirectX 11のバッファを更新するための構造体
		 */
		struct BufferUpdateDesc {
			ComPtr<ID3D11Buffer> buffer;
			const void* data = nullptr;
			size_t size = (std::numeric_limits<size_t>::max)();
			bool isDelete = true;

			bool isValid() const {
				return buffer != nullptr && data != nullptr && size != (std::numeric_limits<size_t>::max)();
			}

			bool operator==(const BufferUpdateDesc& other) const {
				return buffer.Get() == other.buffer.Get();
			}
		};
		/**
		 * @brief バッファの管理クラス。DirectX 11のバッファを作成、キャッシュ、更新する機能を提供する。
		 */
		class BufferManager : public ResourceManagerBase<
			BufferManager, BufferHandle, BufferCreateDesc, BufferData>
		{
		public:
			static constexpr inline uint32_t MAX_PENDING_UPDATE_NUM = 1024;

			/**
			 * @brief コンストラクタ
			 * @param device DX11のデバイス
			 * @param context　DX11のデバイスコンテキスト
			 */
			BufferManager(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
				: device(device), context(context) {
			}
			/**
			 * @brief デストラクタ
			 */
			~BufferManager() {
				for (auto& pendings : pendingUpdates) {
					for (auto& update : pendings) {
						if (update.data && update.isDelete) delete update.data;
					}
				}
			}
			/**
			 * @brief 既存検索（名前ベース）
			 * @param desc バッファ作成記述子
			 * @return std::optional<BufferHandle> 既存のバッファハンドル、存在しない場合は std::nullopt
			 */
			std::optional<BufferHandle> FindExisting(const BufferCreateDesc& desc) noexcept {
				if (auto it = nameToHandle.find(desc.name); it != nameToHandle.end())
					return it->second;
				return std::nullopt;
			}
			/**
			 * @brief キー登録
			 * @param desc バッファ作成記述子
			 * @param h 登録するバッファハンドル
			 */
			void RegisterKey(const BufferCreateDesc& desc, BufferHandle h) {
				nameToHandle.emplace(desc.name, h);
			}
			/**
			 * @brief リソース作成
			 * @param desc バッファ作成記述子
			 * @param h 登録するバッファハンドル
			 * @return DX11BufferData 作成されたバッファデータ
			 */
			BufferData CreateResource(const BufferCreateDesc& desc, BufferHandle h) {
				BufferData cb{};

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

				assert(desc.bindFlags != D3D11_BIND_CONSTANT_BUFFER || desc.size % 16 == 0 && "定数バッファのサイズが16倍数ではありません");

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

				BufferCacheKey key{ HashBufferContent(data, size), size };
				if (auto it = cbvCache.find(key); it != cbvCache.end()) {
					AddRef(it->second);
					return it->second;
				}
				// 初回作成：匿名名で Add（Add が +1 を返す）
				BufferHandle h;
				BufferCreateDesc desc = {
					.name = "auto_cb_" + std::to_string(key.hash),
					.size = size,
					.initialData = data
				};
				Add(desc, h);

				cbvCache[key] = h;
				handleToCacheKey[h.index] = key;
				return h;
			}
			/**
			 * @brief バッファの内容を遅延で更新するためにキューに追加
			 * @param desc バッファ作成記述子
			 */
			void UpdateBuffer(const BufferUpdateDesc& desc, uint16_t slot) noexcept {
				uint32_t count = pendingCount[slot].fetch_add(1, std::memory_order_acq_rel);
				if (count >= MAX_PENDING_UPDATE_NUM) {
					//LOG_ERROR("最大更新処理数に達しました");
					return;
				}

				pendingUpdates[slot][count] = desc;
			}
			/**
			 * @brief 保留中のバッファ更新を処理
			 */
			void PendingUpdates(size_t frameIndex) {

				uint16_t slot = frameIndex % RENDER_BUFFER_COUNT;
				uint32_t count = pendingCount[slot].load(std::memory_order_relaxed);

				if (count > 0) {
					auto& pendings = pendingUpdates[slot];

					for (uint32_t i = 0; i < count; ++i) {
						auto& update = pendings[i];
						assert(update.isValid() && "バッファの更新情報が正しくありません");

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

					pendingCount[slot].store(0, std::memory_order_release);
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
			/**
			 * @brief CreateSRV: バッファ用のシェーダーリソースビューを作成する関数
			 * @param buffer バッファのComPtr
			 * @param desc シェーダーリソースビューの記述子
			 * @return ID3D11ShaderResourceViewのComPtr
			 */
			ComPtr<ID3D11ShaderResourceView> CreateSRV(ComPtr<ID3D11Buffer> buffer, D3D11_SHADER_RESOURCE_VIEW_DESC desc) const {
				ComPtr<ID3D11ShaderResourceView> srv;
				HRESULT hr = device->CreateShaderResourceView(buffer.Get(), &desc, &srv);
				if (FAILED(hr)) {
					assert(false && "Failed to create shader resource view");
				}
				return srv;
			}
			/**
			 * @brief CreateUAV: バッファ用のアンオーダードアクセスビューを作成する関数
			 * @param buffer バッファのComPtr
			 * @param desc アンオーダードアクセスビューの記述子
			 * @return ID3D11UnorderedAccessViewのComPtr
			 */
			ComPtr<ID3D11UnorderedAccessView> CreateUAV(ComPtr<ID3D11Buffer> buffer, D3D11_UNORDERED_ACCESS_VIEW_DESC desc) const {
				ComPtr<ID3D11UnorderedAccessView> uav;
				HRESULT hr = device->CreateUnorderedAccessView(buffer.Get(), &desc, &uav);
				if (FAILED(hr)) {
					assert(false && "Failed to create unordered access view");
				}
				return uav;
			}
		private:
			ID3D11Device* device;
			ID3D11DeviceContext* context;
			std::unordered_map<std::string, BufferHandle> nameToHandle;
			std::unordered_map<BufferCacheKey, BufferHandle, BufferCacheKeyHash> cbvCache;

			std::unordered_map<uint32_t, BufferCacheKey> handleToCacheKey; // key: handle.index

			std::mutex pendingMutex[RENDER_BUFFER_COUNT];
			std::atomic<uint32_t> pendingCount[RENDER_BUFFER_COUNT]{ 0 };
			BufferUpdateDesc pendingUpdates[RENDER_BUFFER_COUNT][MAX_PENDING_UPDATE_NUM]; // 更新待ちのデータ
		};
	}
}
