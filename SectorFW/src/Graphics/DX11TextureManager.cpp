#include "Graphics/DX11/DX11TextureManager.h"

#ifdef _DEBUG
#include "../../third_party/DirectXTex/MDdx64/include/DirectXTex.h"
#else
#include "../../third_party/DirectXTex/MDx64/include/DirectXTex.h"
#endif //! _DEBUG

#include <stdexcept>
#include <cassert>
#include <cstring>
#include <windows.h>

#include "Debug/logger.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace SectorFW {
    namespace Graphics {

        //==================== 小ヘルパ ====================

        // UTF-8 -> Wide
        std::wstring DX11TextureManager::Utf8ToWide(std::string_view s) {
            if (s.empty()) return L"";
            int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
            return w;
        }

        // “フィルタ非対応”か判定
        static bool IsNonFilterable(DXGI_FORMAT f) {
            return IsCompressed(f) || IsTypeless(f) || IsPlanar(f) || IsPalettized(f) ||
                f == DXGI_FORMAT_B8G8R8X8_UNORM || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
                f == DXGI_FORMAT_B5G6R5_UNORM || f == DXGI_FORMAT_B5G5R5A1_UNORM ||
                f == DXGI_FORMAT_B4G4R4A4_UNORM ||
                f == DXGI_FORMAT_R8_UNORM || f == DXGI_FORMAT_A8_UNORM ||
                f == DXGI_FORMAT_R8G8_UNORM;
        }

        // BGRX → BGRA に矯正（X を A=1 に埋める）
        static HRESULT EnsureBGRAIfBGRX(ScratchImage& img, TexMetadata& meta) {
            if (meta.format == DXGI_FORMAT_B8G8R8X8_UNORM ||
                meta.format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) {
                const DXGI_FORMAT target = (meta.format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
                    ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                    : DXGI_FORMAT_B8G8R8A8_UNORM;
                ScratchImage tmp;
                HRESULT hr = Convert(img.GetImages(), img.GetImageCount(), meta,
                    target, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, tmp);
                if (FAILED(hr)) return hr;
                img = std::move(tmp);
                meta = img.GetMetadata();
            }
            return S_OK;
        }

        // sRGB を “metadata だけでなく画像データも” 変換して一致させる
        static HRESULT ForceSRGBConvert(ScratchImage& img, TexMetadata& meta) {
            DXGI_FORMAT s = MakeSRGB(meta.format);
            if (s == DXGI_FORMAT_UNKNOWN || s == meta.format) return S_OK; // 変換不要

            ScratchImage tmp;
            HRESULT hr = Convert(img.GetImages(), img.GetImageCount(), meta,
                s, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, tmp);
            if (FAILED(hr)) return hr;
            img = std::move(tmp);
            meta = img.GetMetadata();
            return S_OK;
        }

        // ミップチェーン生成（堅牢版）
        static void EnsureMipChain(ScratchImage& img,
            TexMetadata& meta,
            bool forceSRGB,
            size_t maxGeneratedMips)
        {
            // 既にミップありなら何もしない
            if (meta.mipLevels > 1) return;

            // 1) BGRX→BGRA
            EnsureBGRAIfBGRX(img, meta);

            // 2) 非フィルタ形式は事前にフィルタ可能形式へ
            if (IsNonFilterable(meta.format)) {
                const bool wantSRGB = forceSRGB || IsSRGB(meta.format);
                const DXGI_FORMAT target = wantSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : DXGI_FORMAT_R8G8B8A8_UNORM;
                ScratchImage conv;
                HRESULT hr = Convert(img.GetImages(), img.GetImageCount(), meta,
                    target, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, conv);
                if (SUCCEEDED(hr)) {
                    img = std::move(conv);
                    meta = img.GetMetadata();
                }
            }

            // 3) sRGB 強制は “必ず Convert” で format/画像を一致
            if (forceSRGB) {
                ForceSRGBConvert(img, meta);
            }

            // 4) フィルタ設定（sRGB 入力ならガンマ補正）
            TEX_FILTER_FLAGS filter = TEX_FILTER_DEFAULT;
            if (IsSRGB(meta.format)) filter |= TEX_FILTER_SRGB;
            if (meta.GetAlphaMode() == TEX_ALPHA_MODE_PREMULTIPLIED)
                filter |= TEX_FILTER_SEPARATE_ALPHA;

            // 5) 生成
            ScratchImage mip;
            HRESULT hr = GenerateMipMaps(img.GetImages(), img.GetImageCount(), meta,
                filter,
                (maxGeneratedMips > 0 ? maxGeneratedMips : 0),
                mip);
            if (SUCCEEDED(hr)) {
                img = std::move(mip);
                meta = img.GetMetadata();
                return;
            }

            // 6) それでも失敗 → 安全形式へ落として再試行
            {
                const bool wantSRGB = IsSRGB(meta.format);
                const DXGI_FORMAT target = wantSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : DXGI_FORMAT_R8G8B8A8_UNORM;
                ScratchImage conv2;
                hr = Convert(img.GetImages(), img.GetImageCount(), meta,
                    target, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, conv2);
                if (SUCCEEDED(hr)) {
                    img = std::move(conv2);
                    meta = img.GetMetadata();

                    // sRGB 再設定
                    filter = TEX_FILTER_DEFAULT;
                    if (IsSRGB(meta.format)) filter |= TEX_FILTER_SRGB;
                    if (meta.GetAlphaMode() == TEX_ALPHA_MODE_PREMULTIPLIED)
                        filter |= TEX_FILTER_SEPARATE_ALPHA;

                    ScratchImage mip2;
                    hr = GenerateMipMaps(img.GetImages(), img.GetImageCount(), meta,
                        filter,
                        (maxGeneratedMips > 0 ? maxGeneratedMips : 0),
                        mip2);
                    if (SUCCEEDED(hr)) {
                        img = std::move(mip2);
                        meta = img.GetMetadata();
                        return;
                    }
                }
            }

            // 7) ここまで失敗 → ミップ無しで続行（ログ出力）
            char buf[256];
            sprintf_s(buf,
                "[MipGen] E_FAIL: fmt=%d w=%zu h=%zu mips=%zu arr=%zu depth=%zu alpha=%d\n",
                int(meta.format), meta.width, meta.height, meta.mipLevels,
                meta.arraySize, meta.depth, int(meta.GetAlphaMode()));
            OutputDebugStringA(buf);
        }

        //==================== SRV 作成 ====================
        ComPtr<ID3D11ShaderResourceView>
            CreateSRV(ID3D11Resource* tex, const TexMetadata& md, ID3D11Device* device, bool forceSRGB) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = md.format;

            // --- SRGBフラグ付け ---
            if (forceSRGB) {
                DXGI_FORMAT srgbFmt = MakeSRGB(sd.Format);
                if (srgbFmt != DXGI_FORMAT_UNKNOWN) {
                    sd.Format = srgbFmt;
                }
            }

            switch (md.dimension) {
            case TEX_DIMENSION_TEXTURE1D:
                if (md.arraySize > 1) {
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                    sd.Texture1DArray.MostDetailedMip = 0;
                    sd.Texture1DArray.MipLevels = (UINT)md.mipLevels;
                    sd.Texture1DArray.FirstArraySlice = 0;
                    sd.Texture1DArray.ArraySize = (UINT)md.arraySize;
                }
                else {
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                    sd.Texture1D.MostDetailedMip = 0;
                    sd.Texture1D.MipLevels = (UINT)md.mipLevels;
                }
                break;

            case TEX_DIMENSION_TEXTURE2D:
                if (md.IsCubemap()) {
                    sd.ViewDimension = (md.arraySize > 6) ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY
                        : D3D11_SRV_DIMENSION_TEXTURECUBE;
                    if (sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE) {
                        sd.TextureCube.MostDetailedMip = 0;
                        sd.TextureCube.MipLevels = (UINT)md.mipLevels;
                    }
                    else {
                        sd.TextureCubeArray.MostDetailedMip = 0;
                        sd.TextureCubeArray.MipLevels = (UINT)md.mipLevels;
                        sd.TextureCubeArray.First2DArrayFace = 0;
                        sd.TextureCubeArray.NumCubes = (UINT)(md.arraySize / 6);
                    }
                }
                else if (md.arraySize > 1) {
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    sd.Texture2DArray.MostDetailedMip = 0;
                    sd.Texture2DArray.MipLevels = (UINT)md.mipLevels;
                    sd.Texture2DArray.FirstArraySlice = 0;
                    sd.Texture2DArray.ArraySize = (UINT)md.arraySize;
                }
                else {
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MostDetailedMip = 0;
                    sd.Texture2D.MipLevels = (UINT)md.mipLevels;
                }
                break;

            case TEX_DIMENSION_TEXTURE3D:
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                sd.Texture3D.MostDetailedMip = 0;
                sd.Texture3D.MipLevels = (UINT)md.mipLevels;
                break;

            default:
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip = 0;
                sd.Texture2D.MipLevels = (UINT)md.mipLevels;
                break;
            }

            ComPtr<ID3D11ShaderResourceView> srv;
            HRESULT hr = device->CreateShaderResourceView(tex, &sd, srv.GetAddressOf());
            if (FAILED(hr)) throw std::runtime_error("CreateShaderResourceView failed.");
            return srv;
        }

        //==================== 本体 ====================

        DX11TextureManager::DX11TextureManager(ID3D11Device* device, std::filesystem::path convertedDir) noexcept
            : device(device), convertedDir(convertedDir) {}

        DX11TextureData DX11TextureManager::CreateResource(const DX11TextureCreateDesc& desc, TextureHandle) {
            TexMetadata meta{};
            ScratchImage img{};
            HRESULT hr = E_FAIL;

            std::string resolved = ResolveConvertedPath(desc.path);

            const auto wpath = Utf8ToWide(resolved);
            auto lower = detail::NormalizePath(resolved);

            if (detail::EndsWithI(lower, ".dds")) {
                hr = LoadFromDDSFile(wpath.c_str(), DDS_FLAGS_NONE, &meta, img);
            }
            else if (detail::EndsWithI(lower, ".tga")) {
                hr = LoadFromTGAFile(wpath.c_str(), &meta, img);
            }
            else if (detail::EndsWithI(lower, ".hdr")) {
                hr = LoadFromHDRFile(wpath.c_str(), &meta, img);
            }
            else {
                // WIC (PNG/JPG/BMP 等) は RGB 展開を強制
                WIC_FLAGS wicFlags = WIC_FLAGS_FORCE_RGB;
                // sRGB 強制をかける場合もここでは “読み込みフラグ” として渡す（WIC の色空間タグ対策）
                if (desc.forceSRGB) wicFlags |= WIC_FLAGS_FORCE_SRGB;
                hr = LoadFromWICFile(wpath.c_str(), wicFlags, &meta, img);
            }
            if (FAILED(hr)) throw std::runtime_error("Failed to load image.");

            // ---- ここからミップ生成（堅牢フロー） ----
            EnsureMipChain(img, meta, desc.forceSRGB, maxGeneratedMips_);

            // GPU テクスチャ化
            ComPtr<ID3D11Resource> tex;
            hr = CreateTexture(device, img.GetImages(), img.GetImageCount(), meta, tex.GetAddressOf());
            if (FAILED(hr)) {
                char buf[256];
                sprintf_s(buf,
                    "CreateTexture E_FAIL: fmt=%d w=%zu h=%zu mips=%zu arr=%zu depth=%zu dim=%d\n",
                    int(meta.format), meta.width, meta.height, meta.mipLevels,
                    meta.arraySize, meta.depth, int(meta.dimension));
                OutputDebugStringA(buf);
                throw std::runtime_error("CreateTexture failed.");
            }

            // SRV
            ComPtr<ID3D11ShaderResourceView> srv = CreateSRV(tex.Get(), meta, device, desc.forceSRGB);

            // 返却
            DX11TextureData out{};
            out.path = desc.path;
            out.srv = std::move(srv);
            return out;
        }

        void DX11TextureManager::RemoveFromCaches(uint32_t idx) {
            const auto& d = this->slots[idx].data;
            detail::DX11TextureKey victim{ detail::NormalizePath(d.path), false };
            std::unique_lock lk(cacheMx_);
            victim.forceSRGB = false; pathToHandle_.erase(victim);
            victim.forceSRGB = true;  pathToHandle_.erase(victim);
        }

        void DX11TextureManager::DestroyResource(uint32_t idx, uint64_t) {
            RemoveFromCaches(idx);
            auto& d = this->slots[idx].data;
            if (d.srv) d.srv.Reset();
        }

        std::string DX11TextureManager::ResolveConvertedPath(const std::string& original) {
            namespace fs = std::filesystem;

            fs::path p = original;
            auto ext = p.extension().string();

            // 例外を避けたい場合
            std::error_code ec;
            fs::path rel = fs::relative(p.parent_path(), assetsDir, ec);
            if (ec) {
				LOG_ERROR("Failed to make relative path: {}", ec.message());
                return original;
            }

            // 変換後のDDSパス候補
            fs::path candidate = convertedDir / rel / p.stem();
            candidate.replace_extension(".dds");

            if (fs::exists(candidate)) {
                return candidate.string(); // DDS を優先
            }
            return original; // なければオリジナルの PNG/JPG
        }

    } // namespace Graphics
} // namespace SectorFW
