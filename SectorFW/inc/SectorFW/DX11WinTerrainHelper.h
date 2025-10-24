// TerrainHelpers.h / .cpp などにどうぞ
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <functional>

using Microsoft::WRL::ComPtr;

//-----------------------------------------------
// 33x33 グリッドの VB/IB を作成（頂点: float2 gridUV）
// indexCountPerPatch には 32*32*6 (= 6144) が入る
//-----------------------------------------------
inline bool CreateTerrainGrid_33x33(
    ID3D11Device* device,
    ComPtr<ID3D11Buffer>& outVB,
    ComPtr<ID3D11Buffer>& outIB,
    uint32_t& outIndexCountPerPatch)
{
    assert(device);
    constexpr uint32_t V = 33;
    constexpr uint32_t Q = V - 1;           // 32
    constexpr uint32_t VertexCount = V * V; // 1089
    constexpr uint32_t IndexCount = Q * Q * 6; // 6144

    struct Vtx { float uv[2]; };

    std::vector<Vtx> verts(VertexCount);
    for (uint32_t j = 0; j < V; ++j) {
        for (uint32_t i = 0; i < V; ++i) {
            const uint32_t idx = j * V + i;
            verts[idx].uv[0] = float(i) / float(Q); // [0,1]
            verts[idx].uv[1] = float(j) / float(Q); // [0,1]
        }
    }

    std::vector<uint32_t> indices(IndexCount);
    uint32_t w = V;
    uint32_t k = 0;
    for (uint32_t j = 0; j < Q; ++j) {
        for (uint32_t i = 0; i < Q; ++i) {
            uint32_t v0 = j * w + i;
            uint32_t v1 = v0 + 1;
            uint32_t v2 = v0 + w;
            uint32_t v3 = v2 + 1;
            // 2 Triangles (v0,v2,v1) , (v1,v2,v3) — 風向はお好みで
            indices[k++] = v0; indices[k++] = v2; indices[k++] = v1;
            indices[k++] = v1; indices[k++] = v2; indices[k++] = v3;
        }
    }

    // VB
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = UINT(verts.size() * sizeof(Vtx));
        bd.Usage = D3D11_USAGE_IMMUTABLE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = verts.data();

        ComPtr<ID3D11Buffer> vb;
        HRESULT hr = device->CreateBuffer(&bd, &init, vb.GetAddressOf());
        if (FAILED(hr)) return false;
        outVB = vb;
    }
    // IB
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = UINT(indices.size() * sizeof(uint32_t));
        bd.Usage = D3D11_USAGE_IMMUTABLE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = indices.data();

        ComPtr<ID3D11Buffer> ib;
        HRESULT hr = device->CreateBuffer(&bd, &init, ib.GetAddressOf());
        if (FAILED(hr)) return false;
        outIB = ib;
    }

    outIndexCountPerPatch = IndexCount;
    return true;
}

//-----------------------------------------------
// R32_FLOAT 1ch の Immutable テクスチャを作成
//-----------------------------------------------
inline bool CreateFloat1TextureSRV(
    ID3D11Device* device,
    uint32_t width, uint32_t height,
    const float* pixels, uint32_t rowFloatStride, // 1行あたりの float 数（=width が普通）
    ComPtr<ID3D11ShaderResourceView>& outSRV)
{
    assert(device && pixels);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels;
    init.SysMemPitch = rowFloatStride * sizeof(float);

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, &init, &tex))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), &sd, &srv))) return false;
    outSRV = srv;
    return true;
}

//-----------------------------------------------
// 画像ファイルから高さSRVを作成（R32_FLOAT・1ch）
// - PNG/JPG などの LDR → 輝度(Y) を 0..1 にして float 化
// - DDS で既に 1ch/float の場合はそのまま
// ※ DirectXTex の導入がある場合はそれを使ってもOK。
//   ここでは WIC 読み込み前提の簡易版（自前変換）例を示します。
//-----------------------------------------------
#if __has_include(<wincodec.h>)
#include <wincodec.h> // WIC
#pragma comment(lib, "windowscodecs.lib")

inline bool CreateOrLoadHeightSRV_FromFile(
    ID3D11Device* device,
    const std::wstring& path,
    float heightScaleMeters,                 // テクスチャ値(0..1)→実高さ[m]にスケール
    ComPtr<ID3D11ShaderResourceView>& outSRV)
{
    assert(device);
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)))) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder))) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    // 32bpp BGRA に統一して読み出す
    ComPtr<IWICFormatConverter> conv;
    factory->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    std::vector<uint8_t> bgra(size_t(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, UINT(bgra.size()), bgra.data()))) return false;

    // BGRA → 輝度（0..1）→ 高さ[m]（R32_FLOAT 1ch）
    std::vector<float> height(size_t(w) * h);
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        const uint8_t B = bgra[i * 4 + 0];
        const uint8_t G = bgra[i * 4 + 1];
        const uint8_t R = bgra[i * 4 + 2];
        // Rec.709 輝度
        float y = (0.2126f * (R / 255.0f) + 0.7152f * (G / 255.0f) + 0.0722f * (B / 255.0f));
        height[i] = y * heightScaleMeters;
    }

    return CreateFloat1TextureSRV(device, w, h, height.data(), w, outSRV);
}
#endif

//-----------------------------------------------
// 手続き生成で高さSRVを作成（R32_FLOAT・1ch）
//   gen(x,y) はワールド/テクスチャ座標に応じた高さ[m]を返す関数
//-----------------------------------------------
template<class HeightGenerator>
inline bool CreateOrLoadHeightSRV_Procedural(
    ID3D11Device* device,
    uint32_t width, uint32_t height,
    HeightGenerator&& gen,                  // float gen(float x, float y)
    ComPtr<ID3D11ShaderResourceView>& outSRV)
{
    std::vector<float> hmap(size_t(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            hmap[size_t(y) * width + x] = gen(x, y);
        }
    }
    return CreateFloat1TextureSRV(device, width, height, hmap.data(), width, outSRV);
}

//-----------------------------------------------
// フロントAPI：ファイル or 手続き生成を選べる
//-----------------------------------------------
struct HeightCreateDesc {
    // いずれかを使用
    std::wstring filePath;        // 画像から
    uint32_t procWidth = 0;       // 手続き生成
    uint32_t procHeight = 0;

    // 画像→高さのスケール [m]
    float heightScaleMeters = 100.0f;

    // 手続き生成用（オプション）
    // 例：Perlin パラメータなどを捕まえたラムダを渡す
    // float operator()(uint32_t x, uint32_t y) を満たす関数オブジェクト
    std::function<float(uint32_t, uint32_t)> generator;
};

inline bool CreateOrLoadHeightSRV(
    ID3D11Device* device,
    const HeightCreateDesc& desc,
    ComPtr<ID3D11ShaderResourceView>& outSRV)
{
#if __has_include(<wincodec.h>)
    if (!desc.filePath.empty()) {
        return CreateOrLoadHeightSRV_FromFile(device, desc.filePath, desc.heightScaleMeters, outSRV);
    }
#endif
    if (desc.procWidth > 0 && desc.procHeight > 0 && desc.generator) {
        return CreateOrLoadHeightSRV_Procedural(device, desc.procWidth, desc.procHeight, desc.generator, outSRV);
    }
    return false;
}
