// D3D11Helpers.hpp
#pragma once
#include <d3d11.h>
#include <wrl.h>
#include <cstdint>

struct StructuredBufferSRVUAV
{
    Microsoft::WRL::ComPtr<ID3D11Buffer>              buf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
};

inline StructuredBufferSRVUAV CreateStructuredBufferSRVUAV(
    ID3D11Device* dev,
    uint32_t elementSize,
    uint32_t elementCount,
    bool createSRV,
    bool createUAV,
    uint32_t uavFlags,               // D3D11_BUFFER_UAV_FLAG_APPEND ‚È‚Ç
    D3D11_USAGE usage,
    uint32_t cpuAccessFlags,
    const void* initialData = nullptr)
{
    StructuredBufferSRVUAV out{};

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = elementSize * elementCount;
    bd.Usage = usage;
    bd.BindFlags = 0;
    if (createSRV) bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (createUAV) bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    bd.CPUAccessFlags = cpuAccessFlags;
    bd.StructureByteStride = elementSize;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = initialData;

    HRESULT hr = dev->CreateBuffer(&bd, initialData ? &sd : nullptr, out.buf.GetAddressOf());
    if(FAILED(hr))
    {
		assert(false && "Failed to create structured buffer");
	}

    if (createSRV)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Format = DXGI_FORMAT_UNKNOWN;
        srvd.Buffer.FirstElement = 0;
        srvd.Buffer.NumElements = elementCount;
        hr = dev->CreateShaderResourceView(out.buf.Get(), &srvd, out.srv.GetAddressOf());
        if (FAILED(hr))
        {
            assert(false && "Failed to create structured buffer SRV");
		}
    }

    if (createUAV)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
        uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Format = DXGI_FORMAT_UNKNOWN;
        uavd.Buffer.FirstElement = 0;
        uavd.Buffer.NumElements = elementCount;
        uavd.Buffer.Flags = uavFlags;
        hr = dev->CreateUnorderedAccessView(out.buf.Get(), &uavd, out.uav.GetAddressOf());
        if (FAILED(hr))
        {
            assert(false && "Failed to create structured buffer UAV");
        }
    }

    return out;
}

struct RawBufferSRVUAV
{
    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
};

inline RawBufferSRVUAV CreateRawBufferSRVUAV(
    ID3D11Device* dev,
    uint32_t byteWidth,
    uint32_t miscFlags,      // ALLOW_RAW_VIEWS / DRAWINDIRECT_ARGS
    bool createSRV,
    bool createUAV,
    const void* initData = nullptr)
{
    RawBufferSRVUAV out{};

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = byteWidth;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = 0;
    if (createSRV) bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (createUAV) bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = miscFlags;
    bd.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = initData;

    HRESULT hr = dev->CreateBuffer(&bd, initData ? &sd : nullptr, out.buf.GetAddressOf());
    if (FAILED(hr))
    {
        assert(false && "Failed to create raw buffer");
	}

    if (createSRV)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        srvd.BufferEx.FirstElement = 0;
        srvd.Format = DXGI_FORMAT_R32_TYPELESS;
        srvd.BufferEx.NumElements = byteWidth / 4;
        srvd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        hr = dev->CreateShaderResourceView(out.buf.Get(), &srvd, out.srv.GetAddressOf());
        if (FAILED(hr))
        {
            assert(false && "Failed to create raw buffer SRV");
        }
    }

    if (createUAV)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
        uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Format = DXGI_FORMAT_R32_TYPELESS;
        uavd.Buffer.FirstElement = 0;
        uavd.Buffer.NumElements = byteWidth / 4;
        uavd.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        hr = dev->CreateUnorderedAccessView(out.buf.Get(), &uavd, out.uav.GetAddressOf());
        if (FAILED(hr))
        {
            assert(false && "Failed to create raw buffer UAV");
		}
    }

    return out;
}

inline ComPtr<ID3D11SamplerState> CreateSamplerState(
    ID3D11Device* dev,
    D3D11_FILTER filter,
    D3D11_TEXTURE_ADDRESS_MODE addressU,
    D3D11_TEXTURE_ADDRESS_MODE addressV,
    D3D11_TEXTURE_ADDRESS_MODE addressW,
    float mipLODBias = 0.0f,
    UINT maxAnisotropy = 1,
    D3D11_COMPARISON_FUNC comparisonFunc = D3D11_COMPARISON_ALWAYS,
    const float borderColor[4] = nullptr,
    float minLOD = 0.0f,
    float maxLOD = D3D11_FLOAT32_MAX)
{
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = addressU;
    desc.AddressV = addressV;
    desc.AddressW = addressW;
    desc.MipLODBias = mipLODBias;
    desc.MaxAnisotropy = maxAnisotropy;
    desc.ComparisonFunc = comparisonFunc;
    if (borderColor)
    {
        desc.BorderColor[0] = borderColor[0];
        desc.BorderColor[1] = borderColor[1];
        desc.BorderColor[2] = borderColor[2];
        desc.BorderColor[3] = borderColor[3];
    }
    else
    {
        desc.BorderColor[0] = 0.0f;
        desc.BorderColor[1] = 0.0f;
        desc.BorderColor[2] = 0.0f;
        desc.BorderColor[3] = 0.0f;
    }
    desc.MinLOD = minLOD;
    desc.MaxLOD = maxLOD;
    ComPtr<ID3D11SamplerState> sampler;
    HRESULT hr = dev->CreateSamplerState(&desc, sampler.GetAddressOf());
    if (FAILED(hr))
    {
        assert(false && "Failed to create sampler state");
    }
    return sampler;
}
