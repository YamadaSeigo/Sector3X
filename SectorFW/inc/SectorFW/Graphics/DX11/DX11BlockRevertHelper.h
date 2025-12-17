#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <execution>

#include "../TerrainClustered.h"
#include "../../Math/AABB.hpp"

#ifdef _DEBUG
#include "../third_party/meshoptimizer/MDdx64/include/meshoptimizer.h"
#else
#include "../third_party/meshoptimizer/MDx64/include/meshoptimizer.h"
#endif//_DEBUG

// Optional (only if you use the LOD generator helpers)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// オプション：品質より速度を優先したい場合は true
#ifndef SFW_USE_SIMPLIFY_SLOPPY
#define SFW_USE_SIMPLIFY_SLOPPY 1
#endif

#include "../LightShadowService.h"


namespace SFW::Graphics::DX11 {

    // ------------------------------------------------------------
    // PODs
    // ------------------------------------------------------------
    struct ClusterRangeU32 { UINT offset; UINT count; }; // count in index units (tri-list)
    struct ClusterLodRange { UINT offset; UINT count; };

    // ------------------------------------------------------------
    // Buffer helpers
    // ------------------------------------------------------------
    inline bool CreateRawUAV(ID3D11Device* dev, UINT byteSize, ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = byteSize;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        HRESULT hr = dev->CreateBuffer(&bd, nullptr, &buf);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Format = DXGI_FORMAT_R32_TYPELESS; // RAW
        ud.Buffer.FirstElement = 0;
        ud.Buffer.NumElements = byteSize / 4; // RAW: elements are 4-byte units
        ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        hr = dev->CreateUnorderedAccessView(buf.Get(), &ud, &uav);
        return SUCCEEDED(hr);
    }

    inline bool CreateIndirectArgs(ID3D11Device* dev, ComPtr<ID3D11Buffer>& buf, UINT width)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = width; // DrawInstancedIndirect: 4 DWORDs
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; // not required but harmless
        bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        bd.CPUAccessFlags = 0;
        return SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &buf));
    }

    inline bool CreateStructuredUInt(ID3D11Device* dev, UINT count, bool asUAV,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11ShaderResourceView>& srv,
        ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = count * sizeof(UINT);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | (asUAV ? D3D11_BIND_UNORDERED_ACCESS : 0);
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        HRESULT hr = dev->CreateBuffer(&bd, nullptr, &buf); if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = count;
        hr = dev->CreateShaderResourceView(buf.Get(), &sd, &srv); if (FAILED(hr)) return false;

        if (asUAV) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = count; ud.Buffer.Flags = 0;
            hr = dev->CreateUnorderedAccessView(buf.Get(), &ud, &uav); if (FAILED(hr)) return false;
        }
        return true;
    }

    inline bool CreateStructuredUInt(ID3D11Device* dev, UINT count, bool asUAV,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11ShaderResourceView>* srv,
        ComPtr<ID3D11UnorderedAccessView>& uav,
        UINT cascadeSize)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = count  * cascadeSize * sizeof(UINT);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | (asUAV ? D3D11_BIND_UNORDERED_ACCESS : 0);
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        HRESULT hr = dev->CreateBuffer(&bd, nullptr, &buf); if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        for (UINT c = 0; c < cascadeSize; ++c) {
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.Buffer.FirstElement = c * count;
            sd.Buffer.NumElements = count;
            hr = dev->CreateShaderResourceView(buf.Get(), &sd, &srv[c]); if (FAILED(hr)) return false;
        }

        if (asUAV) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = count * cascadeSize; ud.Buffer.Flags = 0;
            hr = dev->CreateUnorderedAccessView(buf.Get(), &ud, &uav); if (FAILED(hr)) return false;
        }
        return true;
    }

    inline bool CreateStructured(ID3D11Device* dev, UINT count, UINT stride, UINT bindFlags,
        const void* initData,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11ShaderResourceView>& srv,
        ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = count * stride; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = bindFlags; bd.CPUAccessFlags = 0; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = stride;
        D3D11_SUBRESOURCE_DATA srd{}; srd.pSysMem = initData;
        HRESULT hr = dev->CreateBuffer(&bd, initData ? &srd : nullptr, &buf); if (FAILED(hr)) return false;

        if (bindFlags & D3D11_BIND_SHADER_RESOURCE) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = count; hr = dev->CreateShaderResourceView(buf.Get(), &sd, &srv); if (FAILED(hr)) return false;
        }
        if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_UNKNOWN; ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = count; ud.Buffer.Flags = 0; hr = dev->CreateUnorderedAccessView(buf.Get(), &ud, &uav); if (FAILED(hr)) return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // BlockReservedContext: owns GPU resources and shaders
    // ------------------------------------------------------------
    struct BlockReservedContext {

        struct CSParamsShadowCombined {
            float MainFrustum[6][4];
            float CascadeFrustum[kMaxShadowCascades][6][4];
            float ViewProj[16];
            UINT  MaxVisibleIndices;
            UINT  LodLevels = 3;
            float ScreenSize[2] = { 1980.0f,1080.0f };
            float LodPxThreshold_Main[2] = {400.0f,160.0f};
            float LodPxThreshold_Shadow[2] = { 400.0f,160.0f };
        };

        struct VSParams { float View[16]; float Proj[16]; float ViewProj[16];};

        struct CSParamsCB {
            float planes[6][4]; UINT clusterCount; UINT _pad0, _pad1, _pad2; float VP[16]; float ScreenSize[2]; float LodPxThreshold[2]; UINT LodLevels; UINT _pad3;
        };

        struct VSDepthParams { float View[16]; float Proj[16]; float ViewProj[16]; };

        // RAW counter (4B) and ArgsUAV (16B) + DrawIndirect args
        ComPtr<ID3D11Buffer> counterBuf; ComPtr<ID3D11UnorderedAccessView> counterUAV; // 4B RAW
        ComPtr<ID3D11Buffer> argsUAVBuf; ComPtr<ID3D11UnorderedAccessView> argsUAV;    // 16B RAW
        ComPtr<ID3D11Buffer> argsBuf;                                               // 16B INDIRECT_ARGS

        // Visible indices (uint[]) written by CS and read by VS
        ComPtr<ID3D11Buffer>             visibleBuf;
        ComPtr<ID3D11ShaderResourceView> visibleSRV;
        ComPtr<ID3D11UnorderedAccessView> visibleUAV;

        // Terrain source SRVs
        ComPtr<ID3D11Buffer>             indexPoolBuf;    ComPtr<ID3D11ShaderResourceView> indexPoolSRV;      // t0
        ComPtr<ID3D11Buffer>             clusterRangeBuf; ComPtr<ID3D11ShaderResourceView> clusterRangeSRV;   // t1 (optional for LOD0 only)
        ComPtr<ID3D11Buffer>             aabbMinBuf;      ComPtr<ID3D11ShaderResourceView> aabbMinSRV;        // t2
        ComPtr<ID3D11Buffer>             aabbMaxBuf;      ComPtr<ID3D11ShaderResourceView> aabbMaxSRV;        // t3

        // LOD metadata SRVs
        ComPtr<ID3D11Buffer>             lodRangesBuf;    ComPtr<ID3D11ShaderResourceView> lodRangesSRV;      // t4
        ComPtr<ID3D11Buffer>             lodBaseBuf;      ComPtr<ID3D11ShaderResourceView> lodBaseSRV;        // t5
        ComPtr<ID3D11Buffer>             lodCountBuf;     ComPtr<ID3D11ShaderResourceView> lodCountSRV;       // t6

        // Optional vertex streams (VS pulls)
        ComPtr<ID3D11Buffer> posBuf, nrmBuf, uvBuf;
        ComPtr<ID3D11ShaderResourceView> posSRV, nrmSRV, uvSRV;

        // Shaders
        ComPtr<ID3D11ComputeShader> csCullWrite; // CSCullWrite_Group.cso
        ComPtr<ID3D11ComputeShader> csWriteArgs; // CSWriteArgs.cso
        ComPtr<ID3D11VertexShader>  vs;          // TerrainPull VS (vertex-pull)
        ComPtr<ID3D11VertexShader>  vsDepth;
        ComPtr<ID3D11PixelShader>   ps;          // Terrain PS
        ComPtr<ID3DBlob>            vsBlob;      // for IASetInputLayout(nullptr)

        // Constant buffers
        ComPtr<ID3D11Buffer> cbCS; // CSParams (frustum, VP, screen, LOD)

        ComPtr<ID3D11Buffer> cbCameraFrame;

        // Slots (VS SRVs)
        UINT slotVisible = 0, slotPos = 1, slotNrm = 2, slotUV = 3;

        // Cached counts
        UINT clusterCount = 0;
        UINT maxVisibleIndices = 0;

        ComPtr<ID3D11Buffer> cascadeCountersBuf;
        ComPtr<ID3D11UnorderedAccessView> cascadeCountersUAV; // 4B RAW

        // シャドウ用 VisibleIndices（CS で書いて VS で読む）
        ComPtr<ID3D11Buffer>             shadowVisibleBuf;
        ComPtr<ID3D11ShaderResourceView> shadowVisibleSRV[kMaxShadowCascades];
        ComPtr<ID3D11UnorderedAccessView> shadowVisibleUAV;

        // シャドウ用 ArgsUAV (RAW 16B) + DrawIndirectArgs (16B)
        ComPtr<ID3D11Buffer> shadowArgsUAVBuf;
        ComPtr<ID3D11UnorderedAccessView> shadowArgsUAV;
        ComPtr<ID3D11Buffer> shadowArgsBuf;

		// シャドウ用 CS
        ComPtr<ID3D11ComputeShader> csCullWriteShadow;
        // シャドウ用　Arg生成CS
        ComPtr<ID3D11ComputeShader> csWriteArgsShadow; // CSWriteArgs.cso

		// シャドウ用 CS 定数バッファ (LightViewProj, CascadeSplits, ScreenSize, etc)
        ComPtr<ID3D11Buffer> cbCSShadow;

        // シャドウ描画用 VS 定数バッファ (LightViewProj, World)
        ComPtr<ID3D11Buffer> cbVSShadow;

        // 深度専用 VS（Terrain の VS をそのまま使うなら省略可）
        ComPtr<ID3D11VertexShader>  vsShadow;
        // PS は DepthOnly なら nullptr でよいので省略しても OK

        // ---------- Creation helpers ----------
        bool Init(ID3D11Device* dev,
            const wchar_t* csCullPath,
            const wchar_t* csShadowCullPath,
            const wchar_t* csArgsPath,
            const wchar_t* csShadowArgsPath,
            const wchar_t* vsPath,
			const wchar_t* vsDepthPath,
            const wchar_t* psPath,
            UINT maxVisibleIndices_)
        {
            HRESULT hr;
            maxVisibleIndices = maxVisibleIndices_;
            // RAW counter (4B) & ArgsUAV (16B)
            if (!CreateRawUAV(dev, 4, counterBuf, counterUAV)) return false;
            if (!CreateRawUAV(dev, 16, argsUAVBuf, argsUAV)) return false;
			// Indirect args (16B)
            if (!CreateIndirectArgs(dev, argsBuf, 16)) return false;
            // Visible indices (uint) as UAV+SRV
            if (!CreateStructuredUInt(dev, maxVisibleIndices, true, visibleBuf, visibleSRV, visibleUAV)) return false;

			//　シャドウ用 RAW counter (4B)
            if (!CreateRawUAV(dev, 4 * kMaxShadowCascades, cascadeCountersBuf, cascadeCountersUAV)) return false;

            // シャドウ用 VisibleIndices
            if (!CreateStructuredUInt(dev, maxVisibleIndices,
                true, shadowVisibleBuf, shadowVisibleSRV, shadowVisibleUAV, kMaxShadowCascades))
                return false;

            // sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS) 20 * カスケード数
			UINT RAWShadowArgsSize = kMaxShadowCascades * sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS); // 20B * カスケード数

            // シャドウ用 ArgsUAV (RAW  B)
            if (!CreateRawUAV(dev, RAWShadowArgsSize, shadowArgsUAVBuf, shadowArgsUAV))
                return false;

            // シャドウ用 DrawIndirectArgs (20 * カスケード数 B)
            if (!CreateIndirectArgs(dev, shadowArgsBuf, RAWShadowArgsSize))
                return false;

            // Compile/load shaders
            ComPtr<ID3DBlob> csBlob;
            hr = D3DReadFileToBlob(csCullPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csCullWrite);
            if (FAILED(hr)) return false;

            csBlob.Reset();
            hr = D3DReadFileToBlob(csShadowCullPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csCullWriteShadow);
            if (FAILED(hr)) return false;

            csBlob.Reset();
            hr = D3DReadFileToBlob(csArgsPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csWriteArgs);
            if (FAILED(hr)) return false;

            csBlob.Reset();
            hr = D3DReadFileToBlob(csShadowArgsPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csWriteArgsShadow);
            if (FAILED(hr)) return false;

            ComPtr<ID3DBlob> psBlob;
            hr = D3DReadFileToBlob(vsPath, vsBlob.GetAddressOf());
            if (FAILED(hr)) return false;
            hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
            if (FAILED(hr)) return false;

            vsBlob.Reset();
            hr = D3DReadFileToBlob(vsDepthPath, vsBlob.GetAddressOf());
            if (FAILED(hr)) return false;
            hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsDepth);
            if (FAILED(hr)) return false;


            hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf());
            if (FAILED(hr)) return false;
            hr = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
            if (FAILED(hr)) return false;



            // ※ vsShadow を別の .cso から作るならここで読み込む
            //   今回は Terrain の vs をそのまま使うなら省略で OK:
            vsShadow = vsDepth;

            // CBs
            auto makeCB = [&](UINT bytes, ComPtr<ID3D11Buffer>& cb) {
                D3D11_BUFFER_DESC d{};
                d.ByteWidth = (bytes + 15) & ~15u;
                d.Usage = D3D11_USAGE_DYNAMIC;
                d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                return SUCCEEDED(dev->CreateBuffer(&d, nullptr, &cb));
                };
            // CS: planes(6*16=96) + ClusterCount(4*4=16) + VP(64) + Screen(8) + LodThresh(8) + LodLevels(4) = ~196 -> round
            if (!makeCB(sizeof(CSParamsCB), cbCS)) return false;

			if (!makeCB(sizeof(CSParamsShadowCombined), cbCSShadow)) return false;

			// VS Shadow: LightViewProj(64) + World(64)
			if (!makeCB(sizeof(VSDepthParams), cbVSShadow)) return false;
            return true;
        }

        // Build SRVs for IndexPool / ClusterRange / AABB from CPU-side arrays
        bool BuildIndexPool(ID3D11Device* dev, const uint32_t* data, UINT poolCount)
        {
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = poolCount * sizeof(uint32_t); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = sizeof(uint32_t);
            D3D11_SUBRESOURCE_DATA srd{ data, 0, 0 };
            HRESULT hr = dev->CreateBuffer(&bd, &srd, &indexPoolBuf); if (FAILED(hr)) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = poolCount;
            hr = dev->CreateShaderResourceView(indexPoolBuf.Get(), &sd, &indexPoolSRV); return SUCCEEDED(hr);
        }

        bool BuildClusterRange(ID3D11Device* dev, const ClusterRangeU32* ranges, UINT rangeCount)
        {
            ComPtr<ID3D11UnorderedAccessView> dummy;
            return CreateStructured(dev, rangeCount, sizeof(ClusterRangeU32), D3D11_BIND_SHADER_RESOURCE, ranges, clusterRangeBuf, clusterRangeSRV, dummy);
        }

        bool BuildClusterAABBs(ID3D11Device* dev, const float* mins3, const float* maxs3, UINT count)
        {
            // min
            {
                D3D11_BUFFER_DESC bd{};
                bd.ByteWidth = count * sizeof(float) * 3;
                bd.Usage = D3D11_USAGE_DEFAULT;
                bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                bd.StructureByteStride = sizeof(float) * 3;
                D3D11_SUBRESOURCE_DATA srd{ mins3, 0, 0 };
                HRESULT hr = dev->CreateBuffer(&bd, &srd, &aabbMinBuf);
                if (FAILED(hr)) return false;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                sd.Format = DXGI_FORMAT_UNKNOWN;
                sd.Buffer.ElementOffset = 0;
                sd.Buffer.ElementWidth = count;
                hr = dev->CreateShaderResourceView(aabbMinBuf.Get(), &sd, &aabbMinSRV);
                if (FAILED(hr)) return false;
            }
            // max
            {
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth = count * sizeof(float) * 3; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = sizeof(float) * 3;
                D3D11_SUBRESOURCE_DATA srd{ maxs3, 0, 0 }; HRESULT hr = dev->CreateBuffer(&bd, &srd, &aabbMaxBuf); if (FAILED(hr)) return false;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = count; hr = dev->CreateShaderResourceView(aabbMaxBuf.Get(), &sd, &aabbMaxSRV); if (FAILED(hr)) return false;
            }
            clusterCount = count;
            return true;
        }

        bool BuildVertexStreams(ID3D11Device* dev, const float* pos3, const float* nrm3, const float* uv2, UINT vertCount)
        {
            auto makeStream = [&](const void* src, UINT stride, UINT count, ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv) {
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth = count * stride; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = stride; D3D11_SUBRESOURCE_DATA srd{ src, 0, 0 }; HRESULT hr = dev->CreateBuffer(&bd, &srd, &buf); if (FAILED(hr)) return false; D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = count; hr = dev->CreateShaderResourceView(buf.Get(), &sd, &srv); return SUCCEEDED(hr);
                };
            bool ok = true;
            if (pos3) ok &= makeStream(pos3, sizeof(float) * 3, vertCount, posBuf, posSRV);
            if (nrm3) ok &= makeStream(nrm3, sizeof(float) * 3, vertCount, nrmBuf, nrmSRV);
            if (uv2)  ok &= makeStream(uv2, sizeof(float) * 2, vertCount, uvBuf, uvSRV);
            return ok;
        }

        // ---- LOD SRVs ----
        bool BuildLodSrvs(ID3D11Device* dev,
            const std::vector<ClusterLodRange>& ranges,
            const std::vector<uint32_t>& lodBase,
            const std::vector<uint32_t>& lodCount)
        {
            struct RangePod { UINT offset; UINT count; };
            std::vector<RangePod> pods(ranges.size());
            for (size_t i = 0; i < ranges.size(); ++i) { pods[i].offset = ranges[i].offset; pods[i].count = ranges[i].count; }
            ComPtr<ID3D11UnorderedAccessView> dummy;
            if (!CreateStructured(dev, (UINT)pods.size(), sizeof(RangePod), D3D11_BIND_SHADER_RESOURCE, pods.data(), lodRangesBuf, lodRangesSRV, dummy)) return false;
            if (!CreateStructured(dev, (UINT)lodBase.size(), sizeof(uint32_t), D3D11_BIND_SHADER_RESOURCE, lodBase.data(), lodBaseBuf, lodBaseSRV, dummy)) return false;
            if (!CreateStructured(dev, (UINT)lodCount.size(), sizeof(uint32_t), D3D11_BIND_SHADER_RESOURCE, lodCount.data(), lodCountBuf, lodCountSRV, dummy)) return false;
            return true;
        }

        struct ShadowDepthParams
        {
            // カスケード
            UINT cascadeCount = kMaxShadowCascades;
            UINT  lodLevels = 3;

            // メインカメラ
            ID3D11DepthStencilView* mainDSV = nullptr;
            //D3D11_VIEWPORT          mainViewport{};
            Math::Matrix4x4f        mainViewProj;   // LOD 用
            float                   mainFrustumPlanes[6][4];

            ID3D11DepthStencilView* cascadeDSV[kMaxShadowCascades] = {};
            //D3D11_VIEWPORT          cascadeViewport[kMaxShadowCascades]{};
            float                   lightViewProj[kMaxShadowCascades][16];
            float                   cascadeFrustumPlanes[kMaxShadowCascades][6][4]; // 省略するなら 1 セットでも可

            // 画面サイズ（LOD 用）
            UINT screenW = 0;
            UINT screenH = 0;

            // LOD パラメータ
            float lodT0px = 400.f;
            float lodT1px = 160.f;

            float shadowLodT0px = 800.f;
            float shadowLodT1px = 320.f;
        };

        void RunShadowDepth(
            ID3D11DeviceContext* ctx,
            ComPtr<ID3D11Buffer> cameraCB,
            const ShadowDepthParams& p,
			const D3D11_VIEWPORT* cascadeViewport = nullptr)
        {
			if (p.cascadeCount == 0 || p.cascadeCount > kMaxShadowCascades) [[unlikely]] return;

            //一フレームだけ参照をもらう
            cbCameraFrame = std::move(cameraCB);

            // 0) カウンタをクリア
            {
                static constexpr UINT zeros[5u * kMaxShadowCascades] = { 0 };
                ctx->ClearUnorderedAccessViewUint(counterUAV.Get(), zeros);
				ctx->ClearUnorderedAccessViewUint(cascadeCountersUAV.Get(), zeros);
            }

            // 1) CS_TerrainClusteredCombined で
            //    - visibleBuf（メイン用）
            //    - shadowVisibleBuf（シャドウ用）
            //    にインデックスを書き込む
            {
                ctx->CSSetShader(csCullWriteShadow.Get(), nullptr, 0);

                ID3D11ShaderResourceView* srvs[7] = {
                    indexPoolSRV.Get(),
                    clusterRangeSRV.Get(),
                    aabbMinSRV.Get(),
                    aabbMaxSRV.Get(),
                    lodRangesSRV.Get(),
                    lodBaseSRV.Get(),
                    lodCountSRV.Get()
                };
                ctx->CSSetShaderResources(0, 7, srvs);

                // u0: counterUAV（メイン＆シャドウ共通）
                // u1: visibleUAV（メイン）
                // u2: cascadeCountersUAV（カスケード用カウンタ入り RAW）
                // u3: shadowVisibleUAV（カスケード用 VisibleIndices）
                ID3D11UnorderedAccessView* uavs[4] = {
                    counterUAV.Get(),
                    visibleUAV.Get(),
                    cascadeCountersUAV.Get(),
                    shadowVisibleUAV.Get()
                };
                UINT initial[4] = {
                    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
                };
                ctx->CSSetUnorderedAccessViews(0, 4, uavs, initial);


                // cbCS に「メイン + カスケードのフラスタム」と LOD 情報を詰める
                {
                    D3D11_MAPPED_SUBRESOURCE ms{};
                    ctx->Map(cbCSShadow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

                    auto* csp = reinterpret_cast<CSParamsShadowCombined*>(ms.pData);
                    memcpy(csp->MainFrustum , p.mainFrustumPlanes, sizeof(float) * 24);
                    memcpy(csp->CascadeFrustum, p.cascadeFrustumPlanes, sizeof(csp->CascadeFrustum));
                    csp->MaxVisibleIndices = maxVisibleIndices;
                    csp->LodLevels = p.lodLevels;
                    memcpy(csp->ViewProj, p.mainViewProj.data(), sizeof(float) * 16);
                    csp->ScreenSize[0] = (float)p.screenW;
                    csp->ScreenSize[1] = (float)p.screenH;
                    csp->LodPxThreshold_Main[0] = p.lodT0px;
                    csp->LodPxThreshold_Main[1] = p.lodT1px;
                    csp->LodPxThreshold_Shadow[0] = p.shadowLodT0px;
                    csp->LodPxThreshold_Shadow[1] = p.shadowLodT1px;
                    ctx->Unmap(cbCSShadow.Get(), 0);
                    ctx->CSSetConstantBuffers(4, 1, cbCSShadow.GetAddressOf());
                }

                ctx->Dispatch(clusterCount, 1, 1);

                // 後始末
                constexpr ID3D11UnorderedAccessView* nullUAV[4] = { nullptr,nullptr,nullptr,nullptr };
                UINT zerosInit[4] = { 0,0,0,0 };
                ctx->CSSetUnorderedAccessViews(0, 4, nullUAV, zerosInit);
                ID3D11ShaderResourceView* nullSRV[7] = {};
                ctx->CSSetShaderResources(0, 7, nullSRV);
                ctx->CSSetShader(nullptr, nullptr, 0);
            }

            // 2) CS_WriteArgs で
            //    - counterUAV → argsUAVBuf（メイン）
            //    - cascadeCountersUAV → shadowArgsUAVBuf（シャドウ）
            //    を２回呼んで生成

            {
                ctx->CSSetShader(csWriteArgs.Get(), nullptr, 0);

                // メイン用 (counterUAV → argsUAV)
                {
                    ID3D11UnorderedAccessView* uavsArgs[2] = {
                        counterUAV.Get(),        // Counter
                        argsUAV.Get()            // ArgsUAV (メイン用)
                    };
                    UINT initCounts[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
                    ctx->CSSetUnorderedAccessViews(0, 2, uavsArgs, initCounts);

                    ctx->Dispatch(1, 1, 1);

                    constexpr ID3D11UnorderedAccessView* nullU[2] = { nullptr,nullptr };
                    UINT zeroI[2] = { 0,0 };
                    ctx->CSSetUnorderedAccessViews(0, 2, nullU, zeroI);
                }

                ctx->CSSetShader(csWriteArgsShadow.Get(), nullptr, 0);

                // シャドウ用 (cascadeCountersUAV → shadowArgsUAV)
                {
                    ID3D11UnorderedAccessView* uavsArgs[2] = {
                        cascadeCountersUAV.Get(), // Counter (カスケードまとめ用)
                        shadowArgsUAV.Get()       // ArgsUAV (カスケード用)
                    };
                    UINT initCounts[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
                    ctx->CSSetUnorderedAccessViews(0, 2, uavsArgs, initCounts);

                    ctx->Dispatch(1, 1, 1);

                    constexpr ID3D11UnorderedAccessView* nullU[2] = { nullptr,nullptr };
                    UINT zeroI[2] = { 0,0 };
                    ctx->CSSetUnorderedAccessViews(0, 2, nullU, zeroI);
                }

                ctx->CSSetShader(nullptr, nullptr, 0);
            }

            // 3) ArgsUAV → DrawIndirectArgs にコピー
            {
                ctx->CopyResource(argsBuf.Get(), argsUAVBuf.Get());       // メイン
                ctx->CopyResource(shadowArgsBuf.Get(), shadowArgsUAVBuf.Get()); // シャドウ
            }

            // 4) メイン DepthOnly パス
            {
                ctx->OMSetRenderTargets(0, nullptr, p.mainDSV);
                //ctx->RSSetViewports(1, &p.mainViewport);

                ctx->VSSetConstantBuffers(10, 1, cbCameraFrame.GetAddressOf());

                ctx->VSSetShader(vsDepth.Get(), nullptr, 0);
                ctx->PSSetShader(nullptr, nullptr, 0); // DepthOnly

                ID3D11ShaderResourceView* vsSRVs[2] = {
                    visibleSRV.Get(),  // メイン用 VisibleIndices
                    posSRV.Get(),
                    //nrmSRV.Get(),
                    //uvSRV.Get()
                };
                ctx->VSSetShaderResources(20, 2, vsSRVs);

                ctx->IASetInputLayout(nullptr);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                ctx->DrawInstancedIndirect(argsBuf.Get(), 0);
            }

    //        if (cascadeViewport)
    //        {
				//ctx->RSSetViewports(1, cascadeViewport);
    //        }

    //        // 5) カスケードシャドウ DepthOnly パス
    //        for (UINT ci = 0; ci < p.cascadeCount; ++ci)
    //        {
    //            ctx->OMSetRenderTargets(0, nullptr, p.cascadeDSV[ci]);
    //            //ctx->RSSetViewports(1, &p.cascadeViewport[ci]);

    //            // LightViewProj + World
    //            D3D11_MAPPED_SUBRESOURCE ms{};
    //            HRESULT hr = ctx->Map(cbVSShadow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
				//assert(SUCCEEDED(hr) && "頂点シェーダーの定数バッファのマップオープンに失敗しました");
    //            if (SUCCEEDED(hr))
    //            {
    //                auto* vsp = reinterpret_cast<VSDepthParams*>(ms.pData);
    //                //ViewProjしか使用しない
    //                memcpy(vsp->ViewProj, p.lightViewProj[ci], sizeof(float) * 16);

    //                ctx->Unmap(cbVSShadow.Get(), 0);
    //            }

				////各カスケードごとのoffsetで生成したSRVをセット
    //            ctx->VSSetShaderResources(20, 1, shadowVisibleSRV[ci].GetAddressOf());

    //            ctx->VSSetConstantBuffers(10, 1, cbVSShadow.GetAddressOf());

    //            ctx->VSSetShader(vsShadow.Get(), nullptr, 0);
    //            ctx->PSSetShader(nullptr, nullptr, 0);

    //            ID3D11ShaderResourceView* vsSRVs[] = {
    //                posSRV.Get(),
    //                //nrmSRV.Get(),
    //                //uvSRV.Get()
    //            };
    //            ctx->VSSetShaderResources(21, 1, vsSRVs);

    //            ctx->IASetInputLayout(nullptr);
    //            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    //            // オフセットでカスケードごとの Args
    //            ctx->DrawInstancedIndirect(shadowArgsBuf.Get(), ci * sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS));
    //        }

			// 後始末
            constexpr ID3D11ShaderResourceView* nullVs[4] = { nullptr,nullptr,nullptr,nullptr };
            ctx->VSSetShaderResources(20, 4, nullVs);
        }

        void RunColor(ID3D11DeviceContext* ctx)
        {
            ctx->VSSetConstantBuffers(10, 1, cbCameraFrame.GetAddressOf());

            ctx->VSSetShader(vs.Get(), nullptr, 0);
            ctx->PSSetShader(ps.Get(), nullptr, 0);

            ID3D11ShaderResourceView* vsSRVs[4] = {
                visibleSRV.Get(),  // Depth プリパスで作った VisibleIndices_Main
                posSRV.Get(),
                nrmSRV.Get(),
                uvSRV.Get()
            };
            ctx->VSSetShaderResources(20, 4, vsSRVs);

            // 必要であれば PS にシャドウマップ SRV / サンプラをセット

            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // ここでは Arg を流用するだけで、CS は呼ばない
            ctx->DrawInstancedIndirect(argsBuf.Get(), 0);

            ID3D11ShaderResourceView* nullVs[4] = { nullptr,nullptr,nullptr,nullptr };
            ctx->VSSetShaderResources(20, 4, nullVs);

            //参照を解除する
            cbCameraFrame.Reset();
        }
    };

    // ------------------------------------------------------------
    // Convenience: build from TerrainClustered (AoS) into this context
    // ------------------------------------------------------------

    inline bool BuildFromTerrainClustered(ID3D11Device* dev,
        const TerrainClustered& t,
        BlockReservedContext& out)
    {
        if (!out.BuildIndexPool(dev, t.indexPool.data(), (UINT)t.indexPool.size())) return false;

        const UINT ccount = (UINT)t.clusters.size();
        std::vector<ClusterRangeU32> ranges(ccount);
        std::vector<float> mins(ccount * 3), maxs(ccount * 3);
        for (UINT i = 0; i < ccount; ++i) {
            ranges[i].offset = t.clusters[i].indexOffset;
            ranges[i].count = t.clusters[i].indexCount;
            mins[i * 3 + 0] = t.clusters[i].bounds.lb[0]; mins[i * 3 + 1] = t.clusters[i].bounds.lb[1]; mins[i * 3 + 2] = t.clusters[i].bounds.lb[2];
            maxs[i * 3 + 0] = t.clusters[i].bounds.ub[0]; maxs[i * 3 + 1] = t.clusters[i].bounds.ub[1]; maxs[i * 3 + 2] = t.clusters[i].bounds.ub[2];
        }
        if (!out.BuildClusterRange(dev, ranges.data(), ccount)) return false;
        if (!out.BuildClusterAABBs(dev, mins.data(), maxs.data(), ccount)) return false;

        if (out.maxVisibleIndices < (UINT)t.indexPool.size()) {
            out.visibleBuf.Reset(); out.visibleSRV.Reset(); out.visibleUAV.Reset();
            if (!CreateStructuredUInt(dev, (UINT)t.indexPool.size(), true,
                out.visibleBuf, out.visibleSRV, out.visibleUAV)) return false;
            out.maxVisibleIndices = (UINT)t.indexPool.size();
            //VisibleIndicesBuffer作り直し
            // Visible indices (uint) as UAV+SRV
            if (!CreateStructuredUInt(dev, out.maxVisibleIndices, true, out.visibleBuf, out.visibleSRV, out.visibleUAV)) return false;

            // シャドウ用 VisibleIndices
            if (!CreateStructuredUInt(dev, out.maxVisibleIndices,
                true, out.shadowVisibleBuf, out.shadowVisibleSRV, out.shadowVisibleUAV, kMaxShadowCascades))
                return false;
        }

        if (!t.vertices.empty()) {
            const UINT vcount = (UINT)t.vertices.size();
            std::vector<float> pos3(vcount * 3), nrm3(vcount * 3), uv2(vcount * 2);
            for (UINT i = 0; i < vcount; ++i) {
                const auto& v = t.vertices[i];
                pos3[i * 3 + 0] = v.pos.x; pos3[i * 3 + 1] = v.pos.y; pos3[i * 3 + 2] = v.pos.z;
                nrm3[i * 3 + 0] = v.nrm.x; nrm3[i * 3 + 1] = v.nrm.y; nrm3[i * 3 + 2] = v.nrm.z;
                uv2[i * 2 + 0] = v.uv.x;  uv2[i * 2 + 1] = v.uv.y;
            }
            out.BuildVertexStreams(dev, pos3.data(), nrm3.data(), uv2.data(), vcount);
        }
        return true;
    }

    // ------------------------------------------------------------
    // Optional: Generate LODs per cluster using meshoptimizer
    // ------------------------------------------------------------
#ifdef MESHOPTIMIZER_VERSION

    inline void GenerateClusterLODs_meshopt(
        const std::vector<uint32_t>& inIndexPool,
        const std::vector<TerrainClustered::ClusterRange>& inRanges,
        const float* positions, size_t vertexCount, size_t positionStrideBytes,
        const std::vector<float>& lodTargets,
        // outputs
        std::vector<uint32_t>& outIndexPool,
        std::vector<ClusterLodRange>& outLodRanges,
        std::vector<uint32_t>& outLodBase,
        std::vector<uint32_t>& outLodCount)
    {
        outIndexPool.clear(); outLodRanges.clear(); outLodBase.resize(inRanges.size()); outLodCount.resize(inRanges.size());
        const size_t levels = lodTargets.size();
        std::vector<uint32_t> tmp;

        for (size_t cid = 0; cid < inRanges.size(); ++cid) {
            const auto r = inRanges[cid];
            const uint32_t triCount0 = r.indexCount / 3u;
            outLodBase[cid] = (uint32_t)outLodRanges.size();

            // LOD0 as-is
            ClusterLodRange range0{ (UINT)outIndexPool.size(), r.indexCount };
            outIndexPool.insert(outIndexPool.end(), inIndexPool.begin() + r.indexOffset, inIndexPool.begin() + r.indexOffset + r.indexCount);
            outLodRanges.push_back(range0);
            UINT produced = 1;

            float error = r.bounds.extent().length() * 0.01f;

            for (size_t li = 1; li < levels; ++li) {
                const float scale = lodTargets[li];
                const uint32_t targetTris = (uint32_t)(std::max)(1.0f, std::floor(triCount0 * scale));
                const uint32_t targetIdx = targetTris * 3u;

                tmp.assign(inIndexPool.begin() + r.indexOffset, inIndexPool.begin() + r.indexOffset + r.indexCount);
                size_t written = meshopt_simplify(tmp.data(), tmp.data(), r.indexCount, positions, vertexCount, positionStrideBytes, targetIdx, error);
                meshopt_optimizeVertexCache(tmp.data(), tmp.data(), written, vertexCount);

                if (written >= 3) {
                    ClusterLodRange lr{ (UINT)outIndexPool.size(), (UINT)written };
                    outIndexPool.insert(outIndexPool.end(), tmp.begin(), tmp.begin() + written);
                    outLodRanges.push_back(lr);
                    ++produced;
                }
                else {
                    break;
                }
            }
            outLodCount[cid] = produced;
        }
    }

    inline void GenerateClusterLODs_meshopt_fast(
        const std::vector<uint32_t>& inIndexPool,
        const std::vector<TerrainClustered::ClusterRange>& inRanges,
        const float* positions, size_t vertexCount, size_t positionStrideBytes,
        const std::vector<float>& lodTargets,
        // outputs
        std::vector<uint32_t>& outIndexPool,
        std::vector<ClusterLodRange>& outLodRanges,
        std::vector<uint32_t>& outLodBase,
        std::vector<uint32_t>& outLodCount)
    {
        outIndexPool.clear();
        outLodRanges.clear();
        outLodBase.assign(inRanges.size(), 0);
        outLodCount.assign(inRanges.size(), 0);

        const size_t levels = lodTargets.size();
        if (levels == 0 || inRanges.empty()) return;

        // 1) 並列に「各クラスタのローカル出力」を作る
        struct LocalOut {
            std::vector<uint32_t> indices;            // 後で outIndexPool に連結
            std::vector<ClusterLodRange> ranges;      // offset は local 基準（後で +global base）
        };
        std::vector<LocalOut> locals(inRanges.size());

        std::vector<size_t> ids(inRanges.size());
        std::iota(ids.begin(), ids.end(), size_t{ 0 });

        std::for_each(std::execution::par, ids.begin(), ids.end(), [&](size_t cid)
            {
                const auto& r = inRanges[cid];
                if (r.indexCount < 3) return;

                // ---- thread_local scratch ----
                struct Scratch {
                    std::vector<uint32_t> srcIndices;
                    std::vector<uint32_t> remap;
                    std::vector<uint32_t> invRemap;
                    std::vector<uint32_t> localIndices;
                    std::vector<float>  localVerts;
                    std::vector<uint32_t> tmp; // for output / remap back
                };
                Scratch s;

                // 元インデックスを取り出し
                s.srcIndices.assign(inIndexPool.begin() + r.indexOffset,
                    inIndexPool.begin() + r.indexOffset + r.indexCount);

                // 参照頂点だけに縮約
                s.remap.resize(vertexCount);
                size_t unique = meshopt_generateVertexRemap(
                    s.remap.data(), s.srcIndices.data(), s.srcIndices.size(),
                    positions, vertexCount, positionStrideBytes);

                // ローカル頂点/インデックスにリマップ
                s.localVerts.resize(unique * positionStrideBytes);
                meshopt_remapVertexBuffer(s.localVerts.data(), positions, vertexCount,
                    positionStrideBytes, s.remap.data());

                s.localIndices.resize(s.srcIndices.size());
                meshopt_remapIndexBuffer(s.localIndices.data(), s.srcIndices.data(),
                    s.srcIndices.size(), s.remap.data());

                // 逆リマップ表 (local -> global)
                s.invRemap.resize(unique);
                for (uint32_t g = 0; g < (uint32_t)vertexCount; ++g) {
                    uint32_t l = s.remap[g];
                    if (l != ~0u && l < unique) s.invRemap[l] = g;
                }

                LocalOut local;
                local.indices.reserve(r.indexCount); // 少なくとも LOD0

                auto appendLodLocal = [&](const uint32_t* idx, size_t count) {
                    ClusterLodRange lr{ (uint32_t)local.indices.size(), (uint32_t)count };
                    local.indices.insert(local.indices.end(), idx, idx + count);
                    local.ranges.push_back(lr);
                    };

                // LOD0: ローカル→グローバルへ戻して格納
                s.tmp.resize(s.localIndices.size());
                for (size_t i = 0; i < s.localIndices.size(); ++i)
                    s.tmp[i] = s.invRemap[s.localIndices[i]];
                appendLodLocal(s.tmp.data(), s.tmp.size());

                const uint32_t triCount0 = r.indexCount / 3u;
                float error =  r.bounds.extent().length() * 0.05f;

                size_t prevWritten = s.localIndices.size();

                for (size_t li = 1; li < levels; ++li) {
                    const float scale = lodTargets[li];
                    const uint32_t targetTris = (uint32_t)(std::max)(1.0f, std::floor(triCount0 * scale));
                    const size_t   targetIdx = size_t(targetTris) * 3;

                    s.tmp.resize(s.localIndices.size());
                    size_t written = meshopt_simplify(
                        s.tmp.data(), s.localIndices.data(), s.localIndices.size(),
                        s.localVerts.data(), unique, positionStrideBytes,
                        targetIdx, error, meshopt_SimplifyLockBorder);

                    if (written < 3 || written == prevWritten) break;

                    meshopt_optimizeVertexCache(s.tmp.data(), s.tmp.data(), written, unique);
                    prevWritten = written;

                    // ローカル→グローバル
                    for (size_t i = 0; i < written; ++i)
                        s.tmp[i] = s.invRemap[s.tmp[i]];

                    appendLodLocal(s.tmp.data(), written);
                }

                outLodCount[cid] = (uint32_t)local.ranges.size();
                locals[cid] = std::move(local);
            });

        // 2) 単一スレッドで連結（offset をグローバルに直しつつ）
        size_t totalIdx = 0, totalRanges = 0;
        for (auto& l : locals) { totalIdx += l.indices.size(); totalRanges += l.ranges.size(); }
        outIndexPool.reserve(totalIdx);
        outLodRanges.reserve(totalRanges);

        for (size_t cid = 0; cid < locals.size(); ++cid) {
            auto& l = locals[cid];
            outLodBase[cid] = (uint32_t)outLodRanges.size();

            const uint32_t base = (uint32_t)outIndexPool.size();
            outIndexPool.insert(outIndexPool.end(), l.indices.begin(), l.indices.end());

            for (auto lr : l.ranges) {
                lr.offset += base;            // ローカル→グローバル
                outLodRanges.push_back(lr);
            }
            // outLodCount[cid] は並列側で設定済み
        }
    }
#endif

    //============================================================
        // 4レイヤ + スプラット制御 の “Texture2DArray 方式” 支援
        //============================================================

        // Resolve: あなたの資産DBから id -> (path, forceSRGB) を解決
    using ResolveTexturePathFn = bool(*)(uint32_t id, std::string& path, bool& forceSRGB);

    // クラスタ別スプラットの slice を保持・PS 定数で通知
    struct SplatArrayResources {
        // 共通のスプラット Array（t14 に張る想定）
        ComPtr<ID3D11ShaderResourceView> splatArraySRV;
        // PS用
        ComPtr<ID3D11SamplerState>       sampLinearWrap; // s0
        ComPtr<ID3D11Buffer>             cbSplat;        // b1
        // クラスタごとのメタ
        struct PerCluster {
            int   splatSlice = 0;     // Texture2DArray のスライス番号
            float layerTiling[4][2]{}; // 各素材のタイル
            float splatST[2]{ 1,1 };
            float splatOffset[2]{ 0,0 };

        };
        std::vector<PerCluster> perCluster;

    };

    // PS 側のレイアウトに合わせた CB 構造（b1）
    struct SplatCBData {
        float layerTiling[4][2];
        float splatST[2];
        float splatOffset[2];
        int   splatSlice;
        float _pad[3];

    };

    // ------------------------------------------------------------------
        // 共通4素材（アルベド）: クラスタに依らず固定の4枚を t10..t13 に張る
            // ------------------------------------------------------------------
    using ResolveTexturePathFn = bool(*)(uint32_t id, std::string& path, bool& forceSRGB);

    struct CommonMaterialResources {
        // t10..t13 に貼る素材アルベド
        ComPtr<ID3D11ShaderResourceView> layerSRV[4];
        // 共通サンプラ（s0）。既存のスプラットと共有でOK
        ComPtr<ID3D11SamplerState>       sampLinearWrap;
        // 管理用: 指定IDを覚えておく（デバッグ/ホットリロードなど）
        uint32_t materialId[4]{ 0,0,0,0 };

    };

    // クラスタ別に必要な最小情報（PSでインデックスして読む）
    struct ClusterParam {
        int   splatSlice;       // Texture2DArray のスライス
        int   _pad0[3];
        float layerTiling[4][2]; // 各素材のタイル(U,V)
        float splatST[2];        // スプラットUVスケール
        float splatOffset[2];    // スプラットUVオフセット

    };

    // グリッド定数（b2）
    struct alignas(16) TerrainGridCB {
        Math::Vec2f originXZ;     // ワールドX-Zの原点（グリッド(0,0)の左下 or 任意基準）
        Math::Vec2f cellSizeXZ;   // 各クラスタの幅・奥行（ワールド）
        uint32_t dimX;       // クラスタ数X
        uint32_t dimZ;       // クラスタ数Z
        uint32_t _pad[2];

    };

    struct ClusterParamsGPU {
        // SRV で読む StructuredBuffer
        ComPtr<ID3D11Buffer>             sb;     // D3D11_BIND_SHADER_RESOURCE | D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
        ComPtr<ID3D11ShaderResourceView> srv;    // t15 にバインド
        // グリッド用CB（b2）
        ComPtr<ID3D11Buffer>             cbGrid;
        // CPU側ミラー
        std::vector<ClusterParam>        cpu;
        TerrainGridCB                    grid;

    };

    // サンプラと SRV を構築（アルベドは sRGB 推奨）
    inline bool BuildCommonMaterialSRVs(ID3D11Device* dev,
        SFW::Graphics::DX11::TextureManager& texMgr,
        const uint32_t materialIds[4],
        ResolveTexturePathFn resolve,
        CommonMaterialResources& out)
    {
        // s0 (LINEARWRAP) が未作成なら作る
        if (!out.sampLinearWrap) {
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            if (FAILED(dev->CreateSamplerState(&sd, &out.sampLinearWrap))) return false;

        }

        // 素材4をロード
        for (int i = 0; i < 4; ++i) {
            std::string path; bool forceSRGB = true; // アルベドはsRGB
            if (!resolve(materialIds[i], path, forceSRGB)) return false;
            SFW::Graphics::DX11::TextureCreateDesc d{};
            d.path = path; d.forceSRGB = forceSRGB;

            TextureHandle h = {};
            texMgr.Add(d, h); // ResourceManagerBase経由のCreate/キャッシュ
            auto data = texMgr.Get(h);
            auto& td = data.ref();  // TextureData（srv/resource を保持）
            out.layerSRV[i] = td.srv;  // SRVを保持
            out.materialId[i] = materialIds[i];

        }
        return true;
    }

    // t10..t13 に共通素材をセット（描画の先頭で一度だけ呼べばOK）
    inline void BindCommonMaterials(ID3D11DeviceContext* ctx,
        const CommonMaterialResources& R)
    {
        ID3D11ShaderResourceView* mats[4] = {
        R.layerSRV[0].Get(), R.layerSRV[1].Get(),
        R.layerSRV[2].Get(), R.layerSRV[3].Get()
        };
        ctx->PSSetShaderResources(20, 4, mats);                 // t10..t13
        ID3D11SamplerState* samp = R.sampLinearWrap.Get();
        ctx->PSSetSamplers(0, 1, &samp);                        // s0（スプラットと共有）
    }

    // 初期化（サンプラ/CB確保 & perCluster を terrain に合わせて確保）
    inline bool InitSplatArrayResources(ID3D11Device* dev,
        SplatArrayResources& out,
        size_t clusterCount)
    {
        out.perCluster.resize(clusterCount);
        // sampler
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        if (FAILED(dev->CreateSamplerState(&sd, &out.sampLinearWrap))) return false;
        // constant buffer
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(SplatCBData);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &out.cbSplat))) return false;
        return true;
    }

    // ユニークな splatTextureId を収集
    inline void CollectUniqueSplatIds(const TerrainClustered& terrain,
        std::vector<uint32_t>& outUnique)
    {
        std::vector<uint32_t> tmp;
        tmp.reserve(terrain.splat.size());
        for (size_t i = 0; i < terrain.splat.size(); ++i) {
            tmp.push_back(terrain.splat[i].splatTextureId);

        }
        std::sort(tmp.begin(), tmp.end());
        tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
        outUnique = std::move(tmp);
    }

    // ユニークID → slice の辞書を作る
    inline std::unordered_map<uint32_t, int>
        BuildSliceTable(const std::vector<uint32_t>& uniqueIds)
    {
        std::unordered_map<uint32_t, int> m; m.reserve(uniqueIds.size() * 2);
        for (int i = 0; i < (int)uniqueIds.size(); ++i) m.emplace(uniqueIds[i], i);
        return m;
    }

    // Texture2DArray の生成と CopySubresourceRegion によるコピー
    // 条件: 幅・高さ・フォーマット・ミップ数がすべて同一
    inline bool BuildSplatArrayTexture(ID3D11Device* dev,
        ID3D11DeviceContext* ctx, // Copy 用
        TextureManager& texMgr,
        const std::vector<uint32_t>& uniqueSplatIds,
        ResolveTexturePathFn resolve,
        ComPtr<ID3D11ShaderResourceView>& outArraySRV)
    {
        if (uniqueSplatIds.empty()) return false;

        // まず全スライスの元テクスチャ2Dを収集
        struct SliceSrc {
            ComPtr<ID3D11Texture2D> tex2D;
            D3D11_TEXTURE2D_DESC    desc{};
            UINT                    mipLevels = 1;

        };
        std::vector<SliceSrc> slices;
        slices.reserve(uniqueSplatIds.size());

        for (auto id : uniqueSplatIds) {
            std::string path; bool forceSRGB = false; // 重みテクスチャは非sRGB推奨
            if (!resolve(id, path, forceSRGB)) return false;

            TextureCreateDesc d{}; d.path = path; d.forceSRGB = forceSRGB;
            TextureHandle h = {};
            texMgr.Add(d, h); // 既存キャッシュ利用
            auto data = texMgr.Get(h);
            auto& td = data.ref();  // TextureData
            if (!td.resource) return false;

            ComPtr<ID3D11Texture2D> t2d;
            HRESULT hr = td.resource->QueryInterface(IID_PPV_ARGS(&t2d));
            if (FAILED(hr) || !t2d) return false;

            D3D11_TEXTURE2D_DESC desc{}; t2d->GetDesc(&desc);

            // SRGBフラグが付いている場合は非sRGBに寄せたいが、ここでは前提として一致していることを期待
            slices.push_back({ std::move(t2d), desc, desc.MipLevels });

        }

        // 代表値
        const auto w = slices[0].desc.Width;
        const auto h = slices[0].desc.Height;
        const auto fmt = slices[0].desc.Format;
        const auto mips = slices[0].mipLevels;

        // 互換性チェック（異なる場合は失敗）
        for (size_t i = 1; i < slices.size(); ++i) {
            if (slices[i].desc.Width != w ||
                slices[i].desc.Height != h ||
                slices[i].desc.Format != fmt ||
                slices[i].mipLevels != mips) {
                // 必要ならここで DirectXTex を使って “リサイズ/変換” して揃えるが、簡易版は失敗にする
                return false;

            }

        }

        // Array 本体を作成
        D3D11_TEXTURE2D_DESC ad = {};
        ad.Width = w;
        ad.Height = h;
        ad.MipLevels = mips;
        ad.ArraySize = (UINT)slices.size();
        ad.Format = fmt;               // 非sRGB（重み）を想定
        ad.SampleDesc.Count = 1;
        ad.Usage = D3D11_USAGE_DEFAULT;
        ad.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ad.CPUAccessFlags = 0;
        ad.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> arrayTex;
        HRESULT hr = dev->CreateTexture2D(&ad, nullptr, &arrayTex);
        if (FAILED(hr)) return false;

        // ---- CopySubresourceRegion で各スライスの各ミップをコピー ----
        for (UINT slice = 0; slice < (UINT)slices.size(); ++slice) {
            for (UINT mip = 0; mip < mips; ++mip) {
                const UINT dstSub = D3D11CalcSubresource(mip, slice, mips);
                const UINT srcSub = mip; // 元は ArraySize=1 を想定
                ctx->CopySubresourceRegion(
                    arrayTex.Get(), dstSub,
                    0, 0, 0,
                    slices[slice].tex2D.Get(), srcSub,
                    nullptr // 全面コピー
                );

            }

        }

        // SRV を作成
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = ad.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MostDetailedMip = 0;
        sd.Texture2DArray.MipLevels = ad.MipLevels;
        sd.Texture2DArray.FirstArraySlice = 0;
        sd.Texture2DArray.ArraySize = ad.ArraySize;

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = dev->CreateShaderResourceView(arrayTex.Get(), &sd, &srv);
        if (FAILED(hr)) return false;

        outArraySRV = std::move(srv);
        return true;
    }

    // まとめ：Array 構築＋クラスタごとの slice/タイルを詰める
    inline bool BuildClusterSplatArrayResources(ID3D11Device* dev,
        ID3D11DeviceContext* ctx,
        TextureManager& texMgr,
        const TerrainClustered& terrain,
        ResolveTexturePathFn resolve,
        SplatArrayResources& io,
        std::vector<uint32_t>* outUniqueIds = nullptr)
    {
        io.perCluster.resize(terrain.clusters.size());

        // 1) ユニークID収集
        std::vector<uint32_t> uniqueIds; CollectUniqueSplatIds(terrain, uniqueIds);
        if (uniqueIds.empty()) return false;

        // 2) Array を構築
        if (!BuildSplatArrayTexture(dev, ctx, texMgr, uniqueIds, resolve, io.splatArraySRV))
            return false; // サイズやフォーマットが不一致なら false

        // 3) id->slice の辞書
        const auto id2slice = BuildSliceTable(uniqueIds);

        // 4) perCluster に slice と各種スケール/タイルを転写
        for (uint32_t cid = 0; cid < (uint32_t)terrain.clusters.size(); ++cid) {
            const auto& meta = terrain.splat[cid];
            auto it = id2slice.find(meta.splatTextureId);
            if (it == id2slice.end()) return false;

            auto& dst = io.perCluster[cid];
            dst.splatSlice = it->second;

            for (uint32_t li = 0; li < meta.layerCount && li < 4; ++li) {
                dst.layerTiling[li][0] = meta.layers[li].uvTilingU;
                dst.layerTiling[li][1] = meta.layers[li].uvTilingV;

            }
            dst.splatST[0] = meta.splatUVScaleU;
            dst.splatST[1] = meta.splatUVScaleV;
            dst.splatOffset[0] = meta.splatUVOffsetU;
            dst.splatOffset[1] = meta.splatUVOffsetV;

        }

		if (!outUniqueIds) *outUniqueIds = std::move(uniqueIds);

        return true;
    }

    // 描画直前：クラスタ cid のスプラット（slice/タイル）を b1 へ、共通の Array を t14 へ
    inline void BindSplatArrayForCluster(ID3D11DeviceContext* ctx,
        const SplatArrayResources& R,
        uint32_t cid)
    {
        // t14 固定
        ID3D11ShaderResourceView* srv = R.splatArraySRV.Get();
        ctx->PSSetShaderResources(14, 1, &srv);
        // s0
        ID3D11SamplerState* samp = R.sampLinearWrap.Get();
        ctx->PSSetSamplers(0, 1, &samp);

        // b1 更新
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(R.cbSplat.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            const auto& C = R.perCluster[cid];
            SplatCBData cb{};
            std::memcpy(cb.layerTiling, C.layerTiling, sizeof(cb.layerTiling));
            cb.splatST[0] = C.splatST[0];
            cb.splatST[1] = C.splatST[1];
            cb.splatOffset[0] = C.splatOffset[0];
            cb.splatOffset[1] = C.splatOffset[1];
            cb.splatSlice = C.splatSlice;
            std::memcpy(m.pData, &cb, sizeof(cb));
            ctx->Unmap(R.cbSplat.Get(), 0);

        }
        ID3D11Buffer* cbs[] = { R.cbSplat.Get() };
        ctx->PSSetConstantBuffers(1, 1, cbs); // b1
    }



        // StructuredBuffer の作成/更新
    inline bool BuildOrUpdateClusterParamsSB(ID3D11Device* dev,
        ID3D11DeviceContext* ctx,
        ClusterParamsGPU& out)
    {
        const UINT elemSize = sizeof(ClusterParam);
        const UINT count = (UINT)out.cpu.size();
        if (count == 0) return false;

        if (!out.sb) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = elemSize * count;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = elemSize;
            D3D11_SUBRESOURCE_DATA init{ out.cpu.data(), 0, 0 };
            if (FAILED(dev->CreateBuffer(&bd, &init, &out.sb))) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.Buffer.FirstElement = 0;
            sd.Buffer.NumElements = count;
            if (FAILED(dev->CreateShaderResourceView(out.sb.Get(), &sd, &out.srv))) return false;

        }
        else {
            ctx->UpdateSubresource(out.sb.Get(), 0, nullptr, out.cpu.data(), 0, 0);

        }
        return true;
    }

        // グリッドCBの作成/更新
    inline bool BuildOrUpdateTerrainGridCB(ID3D11Device* dev,
        ID3D11DeviceContext* ctx,
        ClusterParamsGPU& out)
    {
        if (!out.cbGrid) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(TerrainGridCB);
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &out.cbGrid))) return false;

        }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(out.cbGrid.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        std::memcpy(m.pData, &out.grid, sizeof(out.grid));
        ctx->Unmap(out.cbGrid.Get(), 0);
        return true;
    }

        // TerrainClustered → ClusterParamsGPU（CPU側配列とGrid定数）へ詰め替え
        //  - unique splat を Texture2DArray にパック済み（splatSlice が確定している）前提
        //  - ここでは slice は呼び出し側から与える（id→slice の辞書を使って設定）
    inline bool FillClusterParamsCPU(const SFW::Graphics::TerrainClustered& terrain,
        const std::unordered_map<uint32_t, int>& id2slice,
        ClusterParamsGPU& out)
    {
        const size_t N = terrain.clusters.size();
        if (terrain.splat.size() != N) return false;
        out.cpu.resize(N);

        for (uint32_t cid = 0; cid < (uint32_t)N; ++cid) {
            const auto& meta = terrain.splat[cid];
            auto it = id2slice.find(meta.splatTextureId);
            if (it == id2slice.end()) return false;

            ClusterParam p{}; p.splatSlice = it->second;
            for (uint32_t li = 0; li < meta.layerCount && li < 4; ++li) {
                p.layerTiling[li][0] = meta.layers[li].uvTilingU;
                p.layerTiling[li][1] = meta.layers[li].uvTilingV;

            }
            p.splatST[0] = meta.splatUVScaleU;
            p.splatST[1] = meta.splatUVScaleV;
            p.splatOffset[0] = meta.splatUVOffsetU;
            p.splatOffset[1] = meta.splatUVOffsetV;
            out.cpu[cid] = p;

        }
        return true;
    }

     // t15 と b2 のバインド（ワンドロー前に一度だけ）
    inline void BindClusterParamsForOneCall(ID3D11DeviceContext* ctx,
        const ClusterParamsGPU& P)
    {
        ID3D11ShaderResourceView* srv = P.srv.Get();
        ctx->PSSetShaderResources(25, 1, &srv); // t15: StructuredBuffer<ClusterParam>
        ID3D11Buffer* cbs[] = { P.cbGrid.Get() };
        ctx->PSSetConstantBuffers(10, 1, cbs);   // b2: TerrainGridCB
    }

        // 便利ユーティリティ：Terrain のグリッド定数を設定
    inline void SetupTerrainGridCB(const Math::Vec2f& originXZ,
        const Math::Vec2f& cellSizeXZ,
        uint32_t dimX, uint32_t dimZ,
        ClusterParamsGPU& out)
    {
        out.grid.originXZ = originXZ;
        out.grid.cellSizeXZ = cellSizeXZ;
        out.grid.dimX = dimX;
        out.grid.dimZ = dimZ;
    }

        // 入力シート（ID）→ ID3D11Texture2D を取得
    inline ComPtr<ID3D11Texture2D> LoadSheetAsTex2D(TextureManager& texMgr,
        uint32_t sheetId,
        ResolveTexturePathFn resolve,
        bool forceSRGB)
    {
        std::string path{}; bool srgbFlag = forceSRGB;
        if (!resolve(sheetId, path, srgbFlag)) return {};
        TextureCreateDesc cd{}; cd.path = path; cd.forceSRGB = srgbFlag;
//#ifdef _DEBUG
//        cd.convertDSS = false;
//#endif
        TextureHandle h = {};
        texMgr.Add(cd, h);
        auto data = texMgr.Get(h);
        auto& td = data.ref(); // srv/resource を保持:contentReference[oaicite:5]{index=5}
        if (!td.resource) return {};
        ComPtr<ID3D11Texture2D> t2d;
        td.resource->QueryInterface(IID_PPV_ARGS(&t2d)); // 失敗時は nullptr
        return t2d;
    }

    // シートを (clustersX×clustersZ) に分割して、各クラスタ用 Texture2D を新規生成し、
      // 各ミップを CopySubresourceRegion でコピーする。
      // 返値: 生成した各クラスタの TextureHandle（cid=cz*clustersXcx の順）
    inline std::vector<TextureHandle>
        BuildClusterSplatTexturesFromSingleSheet(ID3D11Device* dev,
            ID3D11DeviceContext* ctx,
            TextureManager& texMgr,
            ComPtr<ID3D11Texture2D>& sheet,
            uint32_t clustersX, uint32_t clustersZ,
            uint32_t sheetId,
            ResolveTexturePathFn resolve,
            bool sheetIsSRGB = false)
    {
        std::vector<TextureHandle> out; out.reserve(size_t(clustersX) * clustersZ);

        // 1) シートをロード
        sheet = LoadSheetAsTex2D(texMgr, sheetId, resolve, /*forceSRGB=*/sheetIsSRGB);
        if (!sheet) return out;

        D3D11_TEXTURE2D_DESC sd{}; sheet->GetDesc(&sd);
        if (sd.ArraySize != 1) return out; // 2Dのみ想定

        // 2) タイルのピクセル寸法
        if (sd.Width % clustersX != 0 || sd.Height % clustersZ != 0) return out;
        const UINT tileW = sd.Width / clustersX;
        const UINT tileH = sd.Height / clustersZ;
        const UINT srcMipLevels = sd.MipLevels;
        const DXGI_FORMAT fmt = sd.Format; // 重みは非sRGB推奨

        auto IsBC = [&](DXGI_FORMAT f)->bool {
            switch (f) {
            case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
            case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
            case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
                return true;
            default: return false;

            }
            };
        auto FloorLog2 = [](UINT v)->UINT { UINT n = 0; while (v > 1) { v >>= 1; ++n; } return n; };
        auto CalcMaxMips = [&](UINT w, UINT h)->UINT {
            // 非圧縮用: 1 + floor(log2(max(w,h)))
            UINT mw = (w > h) ? w : h; return 1u + FloorLog2(mw);
            };
        auto CalcMaxMipsBC = [&](UINT w, UINT h)->UINT {
            // BCは各ミップで (w>>m),(h>>m) が 4 の倍数かつ ≥4 の範囲まで
            if ((w < 4) || (h < 4)) return 0; // そもそも不可
            if ((w % 4) != 0 || (h % 4) != 0) return 0; // タイルが4の倍数でない
            UINT mw = w / 4, mh = h / 4; // 4の倍数性を維持したままミップれる回数
            // 1 + min(floor(log2(w/4)), floor(log2(h/4)))
            return 1u + ((FloorLog2(mw) < FloorLog2(mh)) ? FloorLog2(mw) : FloorLog2(mh));
            };

        UINT destMipLevels = (std::min)(srcMipLevels, CalcMaxMips(tileW, tileH));
        if (IsBC(fmt)) {
            UINT bcMax = CalcMaxMipsBC(tileW, tileH);
            if (bcMax == 0) return out; // 分割タイルがBC要件を満たさない
            destMipLevels = (std::min)(destMipLevels, bcMax);

        }

        // 3) 各タイル分の Texture2D をレシピ生成（srv/resource を保持）:contentReference[oaicite:6]{index=6} :contentReference[oaicite:7]{index=7}
        for (uint32_t cz = 0; cz < clustersZ; ++cz) {
            for (uint32_t cx = 0; cx < clustersX; ++cx) {
                TextureRecipe rec{};
                rec.width = tileW;
                rec.height = tileH;
                rec.format = fmt;
                rec.mipLevels = destMipLevels;  // シートと同数
                rec.arraySize = 1;
                rec.usage = D3D11_USAGE_DEFAULT;
                rec.bindFlags = D3D11_BIND_SHADER_RESOURCE;
                rec.cpuAccessFlags = 0;
                rec.miscFlags = 0;

                TextureCreateDesc cd{}; cd.recipe = &rec; // path is empty → 生成モード
                TextureHandle h = {};
                texMgr.Add(cd, h);
                auto data = texMgr.Get(h);
                auto& td = data.ref();
                if (!td.resource) { /*安全策*/ continue; }

                // GPUリソース本体を 2D にキャストし、そこへ CopySubresourceRegion
                ComPtr<ID3D11Texture2D> dst;
                td.resource->QueryInterface(IID_PPV_ARGS(&dst)); // 生成物は resource/srv を保持:contentReference[oaicite:8]{index=8}
                if (!dst) continue;

                // 4) 全ミップをコピー
                for (UINT mip = 0; mip < destMipLevels; ++mip) {
                    const UINT mw = (std::max)(1u, tileW >> mip);
                    const UINT mh = (std::max)(1u, tileH >> mip);
                    // subresource は “総ミップ数” が違うので、それぞれの数を渡して計算
                    const UINT srcSub = D3D11CalcSubresource(mip, 0, srcMipLevels);   // sheet 側
                    const UINT dstSub = D3D11CalcSubresource(mip, 0, destMipLevels);  // 生成先

                    // シート上のオフセット（ミップを考慮）
                    const UINT ox = (cx * tileW) >> mip;
                    const UINT oy = (cz * tileH) >> mip;
                    // BCは 4×4 ブロック境界必須
                    if (IsBC(fmt)) {
                        if ((mw < 4) || (mh < 4)) continue;              // このミップはコピー不可
                        if ((ox & 3u) || (oy & 3u) || (mw & 3u) || (mh & 3u)) continue; // 非整列はスキップ
                    }
                    D3D11_BOX box{ ox, oy, 0, ox + mw, oy + mh, 1 };

                    ctx->CopySubresourceRegion(
                        dst.Get(), dstSub,
                        0, 0, 0,
                        sheet.Get(), srcSub,
                        &box
                    );

                }

                out.push_back(h);

            }

        }
        return out;
    }

    inline std::vector<TextureHandle>
        BuildClusterSplatTexturesFromSingleSheet(ID3D11Device* dev,
            ID3D11DeviceContext* ctx,
            TextureManager& texMgr,
            uint32_t clustersX, uint32_t clustersZ,
            uint32_t sheetId,
            ResolveTexturePathFn resolve,
            bool sheetIsSRGB = false)
    {
        ComPtr<ID3D11Texture2D> sheet;
        return BuildClusterSplatTexturesFromSingleSheet(
            dev, ctx, texMgr,
            sheet,
            clustersX, clustersZ,
            sheetId, resolve,
            sheetIsSRGB);
    }


    // ------------------------------------------------------------
    // BuildSplatArrayFromHandles
    //  - BuildClusterSplatTexturesFromSingleSheet() が返す各クラスタ用
    //    TextureHandle 配列から Texture2DArray を構築し、splatArraySRV を作る
    //  - handles の順序 = クラスタID（cid）順（cz*clustersXcx）を想定
    //    もし順序が異なるなら、cid→handlesIndex の並びを別途渡してください
    // ------------------------------------------------------------
    inline bool BuildSplatArrayFromHandles(ID3D11Device* dev,
        ID3D11DeviceContext* ctx,
        TextureManager& texMgr,
        const std::vector<TextureHandle>& handles,
        SplatArrayResources& out /*writes splatArraySRV*/)
    {
        if (handles.empty()) return false;

        // 1) 代表の desc を取得し、全ハンドルの互換性を検証
        ComPtr<ID3D11Texture2D> first;
        {
            auto data = texMgr.Get(handles[0]);
            auto& td0 = data.ref();
            if (!td0.resource) return false;
            if (FAILED(td0.resource->QueryInterface(IID_PPV_ARGS(&first)))) return false;
        }
        D3D11_TEXTURE2D_DESC refd{}; first->GetDesc(&refd);
        if (refd.ArraySize != 1 || refd.SampleDesc.Count != 1) return false; // 2Dのみ

        for (size_t i = 1; i < handles.size(); ++i) {
            auto data = texMgr.Get(handles[i]);
            auto& td = data.ref();
            if (!td.resource) return false;
            ComPtr<ID3D11Texture2D> t2d;
            if (FAILED(td.resource->QueryInterface(IID_PPV_ARGS(&t2d)))) return false;
            D3D11_TEXTURE2D_DESC d{}; t2d->GetDesc(&d);
            if (d.Width != refd.Width ||
                d.Height != refd.Height ||
                d.Format != refd.Format ||
                d.MipLevels != refd.MipLevels ||
                d.ArraySize != 1 ||
                d.SampleDesc.Count != 1)
                return false; // サイズ/フォーマット/ミップ数が一致しない

        }

        // 2) Array テクスチャを作成
        D3D11_TEXTURE2D_DESC ad = refd;
        ad.ArraySize = (UINT)handles.size();
        ad.BindFlags = D3D11_BIND_SHADER_RESOURCE;    // 必須
        ComPtr<ID3D11Texture2D> arrayTex;
        if (FAILED(dev->CreateTexture2D(&ad, nullptr, &arrayTex))) return false;

        // 3) 各スライスへ全ミップをコピー
        const UINT mips = refd.MipLevels;
        for (UINT slice = 0; slice < handles.size(); ++slice) {
            auto data = texMgr.Get(handles[slice]);
            auto& td = data.ref();
            ComPtr<ID3D11Texture2D> src; td.resource->QueryInterface(IID_PPV_ARGS(&src));
            for (UINT mip = 0; mip < mips; ++mip) {
                const UINT srcSub = D3D11CalcSubresource(mip, 0, mips);
                const UINT dstSub = D3D11CalcSubresource(mip, slice, mips);
                ctx->CopySubresourceRegion(arrayTex.Get(), dstSub, 0, 0, 0, src.Get(), srcSub, nullptr);

            }

        }

        // 4) SRV を作成し、out.splatArraySRV に格納
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = ad.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sd.Texture2DArray.MostDetailedMip = 0;
        sd.Texture2DArray.MipLevels = ad.MipLevels;
        sd.Texture2DArray.FirstArraySlice = 0;
        sd.Texture2DArray.ArraySize = ad.ArraySize;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(dev->CreateShaderResourceView(arrayTex.Get(), &sd, &srv))) return false;
        out.splatArraySRV = std::move(srv);
        return true;
    }

    // 生成 TextureHandle 群から、アプリ側IDを割当てて TerrainClustered.splat[] を埋める補助。
    //  - allocateId(h, cx, cz, cid) : TextureHandle → 任意の uint32_t ID を返す（アセットDB登録など）
    //  - layerTiling 決め打ち/読み出しは呼び出し側ルールに合わせてコールバックで指定可能。
    struct LayerTiling { float uvU, uvV; };
    using AllocateSplatIdFn = uint32_t(*)(TextureHandle h, uint32_t cx, uint32_t cz, uint32_t cid);
    using QueryLayerTilingFn = LayerTiling(*)(uint32_t layerIndex, uint32_t cx, uint32_t cz, uint32_t cid);

    inline void AssignClusterSplatsFromHandles(TerrainClustered& terrain,
        uint32_t clustersX, uint32_t clustersZ,
        const std::vector<TextureHandle>& handles,
        AllocateSplatIdFn allocId,
        QueryLayerTilingFn queryLayer = nullptr,
        Math::Vec2f splatUVScale = {1.0f,1.0f},
        Math::Vec2f splatUVOffset = {0.0f,0.0f})
    {
        const size_t N = size_t(clustersX) * clustersZ;
        if (handles.size() != N) return;
        terrain.splat.resize(N);

        for (uint32_t cz = 0; cz < clustersZ; ++cz) {
            for (uint32_t cx = 0; cx < clustersX; ++cx) {
                const uint32_t cid = cz * clustersX + cx;
                auto& sm = terrain.splat[cid];
                sm.layerCount = 4; // 4レイヤブレンド前提（必要に応じて変更）

                // スプラットID（AllocateSplatIdFn で好きなID体系に）
                sm.splatTextureId = allocId(handles[cid], cx, cz, cid);

                // 素材のタイル（既定値 or コールバックで可変）
                for (uint32_t li = 0; li < sm.layerCount; ++li) {
                    LayerTiling t{ 1.0f, 1.0f };
                    if (queryLayer) t = queryLayer(li, cx, cz, cid);
                    sm.layers[li].uvTilingU = t.uvU;
                    sm.layers[li].uvTilingV = t.uvV;

                }
                // スプラットUV（必要に応じて）
                sm.splatUVScaleU = splatUVScale.x; sm.splatUVScaleV = splatUVScale.y;
                sm.splatUVOffsetU = splatUVOffset.x; sm.splatUVOffsetV = splatUVOffset.x;

            }

        }
    }

} // namespace SFW
