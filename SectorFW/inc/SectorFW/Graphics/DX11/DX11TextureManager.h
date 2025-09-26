/*****************************************************************//**
 * @file   DX11TextureManager.h
 * @brief DirectX 11のテクスチャマネージャークラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "_dx11inc.h"
#include "../RenderTypes.h"
#include "Util/ResouceManagerBase.hpp"

#include <unordered_map>
#include <mutex>
#include <optional>
#include <filesystem>

namespace SectorFW
{
	namespace Graphics
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

			struct DX11TextureKey {
				std::string normPath;
				bool forceSRGB{ false };

				bool operator==(const DX11TextureKey& o) const noexcept {
					return forceSRGB == o.forceSRGB && normPath == o.normPath;
				}
			};
			struct DX11TextureKeyHash {
				size_t operator()(const DX11TextureKey& k) const noexcept {
					std::hash<std::string> hs; std::hash<bool> hb;
					size_t h = hs(k.normPath);
					// 合成（適当なミックス）
					return (h ^ (hb(k.forceSRGB) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
				}
			};

		} // namespace detail

		/**
		 * @brief DirectX 11のテクスチャを作成するための構造体
		 */
		struct DX11TextureCreateDesc {
			std::string path;
			bool forceSRGB = false;
		};
		/**
		 * @brief DirectX 11のテクスチャデータを格納する構造体
		 */
		struct DX11TextureData {
			ComPtr<ID3D11ShaderResourceView> srv = nullptr;
		private:
			std::string path; // キャッシュ用のパス

			friend class DX11TextureManager;
		};
		/**
		 * @brief DirectX 11のテクスチャマネージャークラス
		 */
		class DX11TextureManager : public ResourceManagerBase<
			DX11TextureManager, TextureHandle, DX11TextureCreateDesc, DX11TextureData>
		{
			static inline const std::filesystem::path assetsDir = "assets";
			static inline constexpr const char* DefaultConvertedDir = "converted/textures";

		public:
			/**
			 * @brief コンストラクタ
			 * @param device DirectX 11のデバイス
			 */
			explicit DX11TextureManager(ID3D11Device* device, std::filesystem::path convertedDir = DefaultConvertedDir) noexcept;
			/**
			 * @brief 既存のテクスチャを検索する関数
			 * @param desc テクスチャの作成情報
			 * @return std::optional<TextureHandle> 既存のテクスチャハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<TextureHandle> FindExisting(const DX11TextureCreateDesc& desc) noexcept {
				detail::DX11TextureKey k{ detail::NormalizePath(desc.path), desc.forceSRGB };
				std::shared_lock lk(cacheMx_);
				if (auto it = pathToHandle_.find(k); it != pathToHandle_.end()) return it->second;
				return std::nullopt;
			}
			/**
			 * @brief テクスチャのパスとハンドルを登録する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 */
			void RegisterKey(const DX11TextureCreateDesc& desc, TextureHandle h) {
				detail::DX11TextureKey k{ detail::NormalizePath(desc.path), desc.forceSRGB };
				std::unique_lock lk(cacheMx_);
				pathToHandle_.emplace(std::move(k), h);
			}
			/**
			 * @brief テクスチャリソースを作成する関数
			 * @param desc テクスチャの作成情報
			 * @param h 登録するテクスチャハンドル
			 * @return DX11TextureData 作成されたテクスチャデータ
			 */
			DX11TextureData CreateResource(const DX11TextureCreateDesc& desc, TextureHandle h);
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
		private:
			// 変換済みDDSの格納ディレクトリ
			std::string ResolveConvertedPath(const std::string& original);

			// UTF-8 -> Wide
			static std::wstring Utf8ToWide(std::string_view s);

			ID3D11Device* device;

			// 複合キーのキャッシュ（スレッド安全）
			mutable std::shared_mutex cacheMx_;
			std::unordered_map<detail::DX11TextureKey, TextureHandle, detail::DX11TextureKeyHash> pathToHandle_;

			std::filesystem::path convertedDir;

			size_t maxGeneratedMips_{ 0 }; // 0 = 全段
		};
	}
}
