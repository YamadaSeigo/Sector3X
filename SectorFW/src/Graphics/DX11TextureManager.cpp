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

namespace SFW {
    namespace Graphics::DX11 {

        //==================== 小ヘルパ ====================

        // UTF-8 -> Wide
        std::wstring TextureManager::Utf8ToWide(std::string_view s) {
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

        TextureManager::TextureManager(ID3D11Device* device, ID3D11DeviceContext* context, std::filesystem::path convertedDir) noexcept
            : device(device), context(context), convertedDir(convertedDir) {}

        TextureData TextureManager::CreateResource(const TextureCreateDesc& desc, TextureHandle) {
			// パスがある場合はそちらを優先
            if (!desc.path.empty())
            {
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
                TextureData out{};
                out.path = desc.path;
                out.srv = std::move(srv);
                out.resource = std::move(tex);
                return out;
            }

			assert(desc.recipe != nullptr && "not reference of recipe instance"); // recipe 必須

            //==============================
            // B) パス無し：解像度から生成
            //==============================
            D3D11_TEXTURE2D_DESC td{};
            td.Width = desc.recipe->width;
            td.Height = desc.recipe->height;
            td.MipLevels = (desc.recipe->mipLevels == 0) ? 0 : desc.recipe->mipLevels; // 0=フルチェーン
            td.ArraySize = (std::max)(1u, desc.recipe->arraySize);
            td.Format = desc.recipe->format;
            td.SampleDesc.Count = 1;
            td.Usage = desc.recipe->usage;
            td.BindFlags = desc.recipe->bindFlags | D3D11_BIND_SHADER_RESOURCE; // SRV は付けておく
            td.CPUAccessFlags = desc.recipe->cpuAccessFlags;
            td.MiscFlags = desc.recipe->miscFlags;

            ComPtr<ID3D11Texture2D> tex2D;
            HRESULT hr;
            if (desc.recipe->initialData) {
                D3D11_SUBRESOURCE_DATA srd{};
                srd.pSysMem = desc.recipe->initialData;
                srd.SysMemPitch = desc.recipe->initialRowPitch;
                hr = device->CreateTexture2D(&td, &srd, tex2D.GetAddressOf());

            }
            else {
                hr = device->CreateTexture2D(&td, nullptr, tex2D.GetAddressOf());

            }
            if (FAILED(hr)) throw std::runtime_error("CreateTexture2D failed.");

            // SRV 設定（mipLevels=0 の場合は全段 SRV）
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = td.Format;
            if (td.ArraySize > 1) {
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                sd.Texture2DArray.MostDetailedMip = 0;
                sd.Texture2DArray.MipLevels = (td.MipLevels == 0) ? -1 : td.MipLevels;
                sd.Texture2DArray.FirstArraySlice = 0;
                sd.Texture2DArray.ArraySize = td.ArraySize;

            }
            else {
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip = 0;
                sd.Texture2D.MipLevels = (td.MipLevels == 0) ? -1 : td.MipLevels;

            }
            ComPtr<ID3D11ShaderResourceView> srv;
            hr = device->CreateShaderResourceView(tex2D.Get(), &sd, srv.GetAddressOf());
            if (FAILED(hr)) throw std::runtime_error("CreateShaderResourceView failed.");

            TextureData out{};
            out.path.clear();        // 生成物はパス無し
            out.srv = std::move(srv);
			out.resource = std::move(tex2D);
            return out;
        }

        void TextureManager::RemoveFromCaches(uint32_t idx) {
            const auto& d = this->slots[idx].data;
            detail::TextureKey victim{ detail::NormalizePath(d.path), false };
            std::unique_lock lk(cacheMx_);
            victim.forceSRGB = false; pathToHandle_.erase(victim);
            victim.forceSRGB = true;  pathToHandle_.erase(victim);
        }

        void TextureManager::DestroyResource(uint32_t idx, uint64_t) {
            RemoveFromCaches(idx);
            auto& d = this->slots[idx].data;
            if (d.srv) d.srv.Reset();
			if (d.resource) d.resource.Reset();
        }

        //================ 遅延更新 実装 ================
        void TextureManager::UpdateTexture(const TextureUpdateDesc& desc)
        {
            std::lock_guard<std::mutex> lock(updateMx_);
            pendingTexUpdates_.push_back(desc);
        }

        void TextureManager::UpdateTexture(TextureHandle h, const void* pData, UINT rowPitch, UINT depthPitch,
            bool isDelete, const D3D11_BOX* pBox, UINT subresource)
        {
            auto d = Get(h);
            TextureUpdateDesc u{};
            u.tex = d.ref().resource;
            u.subresource = subresource;
            u.pData = pData;
            u.rowPitch = rowPitch;
            u.depthPitch = depthPitch;
            u.isDelete = isDelete;
            if (pBox) { u.useBox = true; u.box = *pBox; }
            UpdateTexture(u);
        }

        void TextureManager::QueueGenerateMips(TextureHandle h)
        {
            auto d = Get(h);
            if (!d.ref().srv) return;
            std::lock_guard<std::mutex> lock(updateMx_);
            pendingGenMips_.push_back({ d.ref().srv });
        }

        void TextureManager::PendingUpdates()
        {
            // ユニーク化： (tex, subresource) で最新だけ残す
            std::vector<TextureUpdateDesc> work;
            {
                std::lock_guard<std::mutex> lock(updateMx_);
                if (pendingTexUpdates_.empty() && pendingGenMips_.empty()) return;

                // 後勝ちマップ（同じキーが複数回あっても最後のものだけ適用）
                struct Key {
                    ID3D11Resource* r; UINT s;
                    bool operator==(const Key& other) const noexcept {
                        return r == other.r && s == other.s;
                    }
                };
                struct KeyHash {
                    size_t operator()(const Key& k) const noexcept {
                        return std::hash<void*>()(k.r) ^ (std::hash<UINT>()(k.s) * 16777619u);

                    }

                };
                std::unordered_map<Key, size_t, KeyHash> lastIndex;
                work = pendingTexUpdates_; // 一旦コピー
                for (size_t i = 0; i < work.size(); ++i) {
                    lastIndex[Key{ work[i].tex.Get(), work[i].subresource }] = i;

                }
                // フィルタして「最後のものだけ」残す
                std::vector<TextureUpdateDesc> filtered;
                filtered.reserve(lastIndex.size());
                for (auto& [k, idx] : lastIndex) filtered.push_back(std::move(work[idx]));
                work.swap(filtered);
            }

            // 適用：UpdateSubresource
            for (auto& u : work) {
                if (!u.tex) continue;
                if (u.useBox) {
                    // 部分更新
                    context->UpdateSubresource(u.tex.Get(), u.subresource, &u.box, u.pData, u.rowPitch, u.depthPitch);

                }
                else {
                    // 全体更新
                    context->UpdateSubresource(u.tex.Get(), u.subresource, nullptr, u.pData, u.rowPitch, u.depthPitch);

                }
                if (u.pData && u.isDelete) {
                    // BufferManager と同様の寿命管理
                    delete[] reinterpret_cast<const std::byte*>(u.pData);

                }

            }

            // 生成ミップ
            std::vector<GenMipsItem> gen;
            {
                std::lock_guard<std::mutex> lock(updateMx_);
                gen.swap(pendingGenMips_);
                pendingTexUpdates_.clear();
            }
            for (auto& g : gen) {
                if (g.srv) context->GenerateMips(g.srv.Get());

            }
        }

        std::string TextureManager::ResolveConvertedPath(const std::string& original) {
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
