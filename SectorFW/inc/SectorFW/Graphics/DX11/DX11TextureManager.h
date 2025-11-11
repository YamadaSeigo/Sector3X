/*****************************************************************//**
 * @file   DX11TextureManager.h
 * @brief DirectX 11のテクスチャマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "../../Util/ResouceManagerBase.hpp"

#include <unordered_map>
#include <mutex>
#include <optional>
#include <filesystem>

namespace SFW
{
	namespace Graphics::DX11
	{
		//= 文字列ヘルパ
		namespace detail {

			inline std::string NormalizePath(std::string path) {
				// 1) すべて '\\' に統一
				std::replace(path.begin(), path.end(), '/', '\\');
				// 2) 末尾の '\\' は削除
				while (!path.empty() && path.back() == '\\') path.pop_back();
				// 3) 小文字化
				std::transform(path.begin(), path.end(), path.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return path;
			}

			inline bool EndsWithI(std::string_view s, std::string_view suf) {
				if (s.size() < suf.size()) return false;
				auto it1 = s.end(); auto it2 = suf.end();
				while (it2 != suf.begin()) {
					--it1; --it2;
					if (std::tolower(static_cast<unsigned char>(*it1)) !=
						std::tolower(static_cast<unsigned char>(*it2))) return false;
				}
				return true;
			}

			struct TextureKey {
				std::string normPath;
				bool forceSRGB{ false };

				bool operator==(const TextureKey& o) const noexcept {
					return forceSRGB == o.forceSRGB && normPath == o.normPath;
				}
			};
			struct TextureKeyHash {
				size_t operator()(const TextureKey& k) const noexcept {
					std::hash<std::string> hs; std::hash<bool> hb;
					size_t h = hs(k.normPath);
					// 合成（適当なミックス）
					return (h ^ (hb(k.forceSRGB) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
				}
			};

		} // namespace detail

		//テクスチャを生データで作成する場合のレシピ
		struct TextureRecipe {
			// 基本は 2D テクスチャ
			uint32_t     width = 0;
			uint32_t     height = 0;
			DXGI_FORMAT  format = DXGI_FORMAT_R8G8B8A8_UNORM;
			uint32_t     mipLevels = 1;     // 0 のときはフルチェーン（自動）にして作成
			uint32_t     arraySize = 1;
			D3D11_USAGE  usage = D3D11_USAGE_DEFAULT;
			UINT         bindFlags = D3D11_BIND_SHADER_RESOURCE; // 必要なら RT/UA も可
			UINT         cpuAccessFlags = 0;                      // DYNAMIC で Map したい場合など
			UINT         miscFlags = 0;                           // 例: D3D11_RESOURCE_MISC_TEXTURECUBE / GENERATE_MIPS
			// 初期データ（トップミップのみ・省略可）
			const void* initialData = nullptr;
			UINT         initialRowPitch = 0;
		};

		/**
		 * @brief DirectX 11のテクスチャを作成するための構造体
		 */
		struct TextureCreateDesc {
			std::string path;
			bool forceSRGB = false;
			bool convertDSS = true;

			// --- 解像度を指定して生成する場合（path が空）----
			TextureRecipe* recipe = nullptr; //指定しない場合はnullptr(必須ではない)
		};
		/**
		 * @brief DirectX 11のテクスチャデータを格納する構造体
		 */
		struct TextureData {
			ComPtr<ID3D11ShaderResourceView> srv = nullptr;
			ComPtr<ID3D11Resource> resource = nullptr; // 必要なら
		private:
			std::string path; // キャッシュ用のパス

			friend class TextureManager;
		};

		struct TextureUpdateDesc {
			// どのテクスチャを更新するか
			ComPtr<ID3D11Resource> tex;
			// どのサブリソースを更新するか（mip/array を含むインデックス）
			UINT subresource = 0;
			// CPU から投げるメモリ（UpdateSubresource 用）
			const void* pData = nullptr;
			UINT rowPitch = 0;    // bytes/row
			UINT depthPitch = 0;  // bytes/slice (3D のみ)
			// 領域限定更新
			bool useBox = false;
			D3D11_BOX box{};
			// 自動 delete の挙動（BufferManager と同様）
			bool isDelete = true;

		};

		/**
		 * @brief DirectX 11のテクスチャマネージャークラス
		 */
		class TextureManager : public ResourceManagerBase<
			TextureManager, TextureHandle, TextureCreateDesc, TextureData>
		{
			static inline const std::filesystem::path assetsDir = "assets";
			static inline constexpr const char* DefaultConvertedDir = "converted/textures";

		public:
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイス
			 */
			explicit TextureManager(ID3D11Device* device,
				ID3D11DeviceContext* context,
				std::filesystem::path convertedDir = DefaultConvertedDir) noexcept;
			/**
			 * @brief 既存のテクスチャを検索する関数
			 * @param desc テクスチャの作成情報
			 * @return std::optional<TextureHandle> 既存のテクスチャハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<TextureHandle> FindExisting(const TextureCreateDesc& desc) noexcept {
				// 生成モード（path 空）のときは名前キャッシュを用いない
				if (desc.path.empty()) return std::nullopt;
				detail::TextureKey k{ detail::NormalizePath(desc.path), desc.forceSRGB };
				std::shared_lock lk(cacheMx_);
				if (auto it = pathToHandle_.find(k); it != pathToHandle_.end()) return it->second;
				return std::nullopt;
			}
			/**
			 * @brief テクスチャのパスとハンドルを登録する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 */
			void RegisterKey(const TextureCreateDesc& desc, TextureHandle h) {
				if (desc.path.empty()) return; // 生成モードはキー登録しない
				detail::TextureKey k{ detail::NormalizePath(desc.path), desc.forceSRGB };
				std::unique_lock lk(cacheMx_);
				pathToHandle_.emplace(std::move(k), h);
			}
			/**
			 * @brief テクスチャリソースを作成する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 * @return DX11TextureData 作成されたテクスチャデータ
			 */
			TextureData CreateResource(const TextureCreateDesc& desc, TextureHandle h);
			/**
			 * @brief キャッシュからテクスチャを削除する関数
			 * @param idx 削除するテクスチャのインデックス
			 */
			void RemoveFromCaches(uint32_t idx);
			/**
			 * @brief テクスチャリソースを破棄する関数
			 * @param idx 破棄するテクスチャのインデックス
			 * @param currentFrame 現在のフレーム番号(未使用)
			 */
			void DestroyResource(uint32_t idx, uint64_t /*currentFrame*/);

			// テクスチャ更新をキューに積む（リソース直指定）
			void UpdateTexture(const TextureUpdateDesc& desc);
			// ハンドルからの更新（最初のサブリソースを想定。mip/array を自前で計算する場合は上を使用）
			void UpdateTexture(TextureHandle h, const void* pData, UINT rowPitch, UINT depthPitch = 0,
				bool isDelete = false, const D3D11_BOX* pBox = nullptr, UINT subresource = 0);

			// 生成済みミップを GPU で作らせる（SRV 必須）→ 遅延キュー
			void QueueGenerateMips(TextureHandle h);

			// 溜まった更新を一括適用（フレームの終わりなどに呼ぶ）
			void PendingUpdates();
		private:
			// 変換済みDDSの格納ディレクトリ
			std::string ResolveConvertedPath(const std::string& original);

			// UTF-8 -> Wide
			static std::wstring Utf8ToWide(std::string_view s);

			ID3D11Device* device;
			ID3D11DeviceContext* context = nullptr;

			// 複合キーのキャッシュ（スレッド安全）
			mutable std::shared_mutex cacheMx_;
			std::unordered_map<detail::TextureKey, TextureHandle, detail::TextureKeyHash> pathToHandle_;

			std::filesystem::path convertedDir;

			size_t maxGeneratedMips_{ 0 }; // 0 = 全段

			//=== 遅延キュー ===
			struct GenMipsItem { ComPtr<ID3D11ShaderResourceView> srv; };
			std::mutex              updateMx_;
			std::vector<TextureUpdateDesc> pendingTexUpdates_;
			std::vector<GenMipsItem>           pendingGenMips_;
		};

		struct CpuImage {
			UINT width = 0, height = 0, stride = 0;
			DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
			std::vector<uint8_t> bytes; // RGBA8想定なら 4*W
		};

		// GPU 2D テクスチャ -> staging -> map で CPU に吸い出す（ミップ0のみ）
		bool ReadTexture2DToCPU(ID3D11Device* dev, ID3D11DeviceContext* ctx,
			ID3D11Texture2D* src, CpuImage& out);
	}
}
