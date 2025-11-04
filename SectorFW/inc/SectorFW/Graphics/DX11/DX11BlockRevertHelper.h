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
#define SFW_USE_SIMPLIFY_SLOPPY 0
#endif


namespace SFW::Graphics {

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

    inline bool CreateIndirectArgs(ID3D11Device* dev, ComPtr<ID3D11Buffer>& buf)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; // DrawInstancedIndirect: 4 DWORDs
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
        ComPtr<ID3D11PixelShader>   ps;          // Terrain PS
        ComPtr<ID3DBlob>            vsBlob;      // for IASetInputLayout(nullptr)

        // Constant buffers
        ComPtr<ID3D11Buffer> cbCS; // CSParams (frustum, VP, screen, LOD)
        ComPtr<ID3D11Buffer> cbVS; // VSParams (ViewProj, World)

        // Slots (VS SRVs)
        UINT slotVisible = 0, slotPos = 1, slotNrm = 2, slotUV = 3;

        // Cached counts
        UINT clusterCount = 0;
        UINT maxVisibleIndices = 0;

        // ---------- Creation helpers ----------
        bool Init(ID3D11Device* dev,
            const wchar_t* csCullPath, const wchar_t* csArgsPath,
            const wchar_t* vsPath, const wchar_t* psPath,
            UINT maxVisibleIndices_)
        {
            HRESULT hr;
            maxVisibleIndices = maxVisibleIndices_;
            // RAW counter (4B) & ArgsUAV (16B)
            if (!CreateRawUAV(dev, 4, counterBuf, counterUAV)) return false;
            if (!CreateRawUAV(dev, 16, argsUAVBuf, argsUAV)) return false;
            // Indirect args
            if (!CreateIndirectArgs(dev, argsBuf)) return false;
            // Visible indices (uint) as UAV+SRV
            if (!CreateStructuredUInt(dev, maxVisibleIndices, true, visibleBuf, visibleSRV, visibleUAV)) return false;

            // Compile/load shaders
            ComPtr<ID3DBlob> csBlob;
            hr = D3DReadFileToBlob(csCullPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csCullWrite); if (FAILED(hr)) return false;
            csBlob.Reset();
            hr = D3DReadFileToBlob(csArgsPath, csBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csWriteArgs); if (FAILED(hr)) return false;

            ComPtr<ID3DBlob> psBlob;
            hr = D3DReadFileToBlob(vsPath, vsBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs); if (FAILED(hr)) return false;
            hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf()); if (FAILED(hr)) return false;
            hr = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps); if (FAILED(hr)) return false;

            // CBs
            auto makeCB = [&](UINT bytes, ComPtr<ID3D11Buffer>& cb) { D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; return SUCCEEDED(dev->CreateBuffer(&d, nullptr, &cb)); };
            // CS: planes(6*16=96) + ClusterCount(4*4=16) + VP(64) + Screen(8) + LodThresh(8) + LodLevels(4) = ~196 -> round
            if (!makeCB(256, cbCS)) return false;
            // VS: ViewProj(64) + World(64)
            if (!makeCB(128, cbVS)) return false;
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
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth = count * sizeof(float) * 3; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = sizeof(float) * 3;
                D3D11_SUBRESOURCE_DATA srd{ mins3, 0, 0 }; HRESULT hr = dev->CreateBuffer(&bd, &srd, &aabbMinBuf); if (FAILED(hr)) return false;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Buffer.ElementOffset = 0; sd.Buffer.ElementWidth = count; hr = dev->CreateShaderResourceView(aabbMinBuf.Get(), &sd, &aabbMinSRV); if (FAILED(hr)) return false;
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

        // ---------- Per-frame: run compute then draw ----------
        // frustumPlanes: 6x float4 (inward, normalized) in the same space as AABB (world)
        // viewProj/world: row_major float4x4
        void Run(ID3D11DeviceContext* ctx,
            const float* frustumPlanes,
            const float viewProj[16],
            const float world[16],
            UINT screenW, UINT screenH,
            float lodT0px = 200.f, float lodT1px = 80.f, UINT lodLevels = 3)
        {
            // Preconditions: SRVs (indexPool/aabb/lodMeta) are built
            // 0) Clear counter
            UINT zeros[4] = { 0,0,0,0 }; ctx->ClearUnorderedAccessViewUint(counterUAV.Get(), zeros);

            // 1) Cull+Write (LOD group reservation)
            ctx->CSSetShader(csCullWrite.Get(), nullptr, 0);
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
            ID3D11UnorderedAccessView* uavsCW[2] = { counterUAV.Get(), visibleUAV.Get() }; UINT keep[2] = { 0xFFFFFFFF, 0xFFFFFFFF }; ctx->CSSetUnorderedAccessViews(0, 2, uavsCW, keep);
            struct CSParamsCB {
                float planes[6][4]; UINT clusterCount; UINT _pad0, _pad1, _pad2; float VP[16]; float ScreenSize[2]; float LodPxThreshold[2]; UINT LodLevels; UINT _pad3;
            } csp{};
            memcpy(csp.planes, frustumPlanes, sizeof(float) * 24);
            csp.clusterCount = clusterCount;
            memcpy(csp.VP, viewProj, sizeof(float) * 16);
            csp.ScreenSize[0] = (float)screenW; csp.ScreenSize[1] = (float)screenH;
            csp.LodPxThreshold[0] = lodT0px; csp.LodPxThreshold[1] = lodT1px; csp.LodLevels = lodLevels;
            D3D11_MAPPED_SUBRESOURCE ms{}; ctx->Map(cbCS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &csp, sizeof(csp)); ctx->Unmap(cbCS.Get(), 0);
            ctx->CSSetConstantBuffers(0, 1, cbCS.GetAddressOf());
            // 1 cluster = 1 group
            ctx->Dispatch(clusterCount, 1, 1);
            ID3D11UnorderedAccessView* nullUAV[2] = { nullptr, nullptr }; UINT zerosInit[2] = { 0,0 }; ctx->CSSetUnorderedAccessViews(0, 2, nullUAV, zerosInit);
            ID3D11ShaderResourceView* nullSRV7[7] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr }; ctx->CSSetShaderResources(0, 7, nullSRV7);

            // 2) Write args
            ctx->CSSetShader(csWriteArgs.Get(), nullptr, 0);
            ID3D11UnorderedAccessView* uavsArgs[2] = { counterUAV.Get(), argsUAV.Get() };
            ctx->CSSetUnorderedAccessViews(0, 2, uavsArgs, keep);
            ctx->Dispatch(1, 1, 1);
            ctx->CSSetUnorderedAccessViews(0, 2, nullUAV, zerosInit);
            ctx->CSSetShader(nullptr, nullptr, 0);

            // 3) Copy ArgsUAV -> Args
            ctx->CopyResource(argsBuf.Get(), argsUAVBuf.Get());

            // 4) Draw (vertex-pull)
            ctx->VSSetShader(vs.Get(), nullptr, 0);
            ctx->PSSetShader(ps.Get(), nullptr, 0);
            struct VSParams { float ViewProj[16]; float World[16]; } vsp{}; memcpy(vsp.ViewProj, viewProj, 64); memcpy(vsp.World, world, 64);
            ctx->Map(cbVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &vsp, sizeof(vsp)); ctx->Unmap(cbVS.Get(), 0);
            ctx->VSSetConstantBuffers(0, 1, cbVS.GetAddressOf());
            ID3D11ShaderResourceView* vsSrvs[4] = { visibleSRV.Get(), posSRV.Get(), nrmSRV.Get(), uvSRV.Get() };
            ctx->VSSetShaderResources(0, 4, vsSrvs);
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->DrawInstancedIndirect(argsBuf.Get(), 0);
            ID3D11ShaderResourceView* nullVs[4] = { nullptr,nullptr,nullptr,nullptr };
            ctx->VSSetShaderResources(0, 4, nullVs);
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
        const std::vector<ClusterRange>& inRanges,
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
        const std::vector<ClusterRange>& inRanges,
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
                float error = /* r.bounds.extent().length() */ 1.0f * 0.01f;

                size_t prevWritten = s.localIndices.size();

                for (size_t li = 1; li < levels; ++li) {
                    const float scale = lodTargets[li];
                    const uint32_t targetTris = (uint32_t)(std::max)(1.0f, std::floor(triCount0 * scale));
                    const size_t   targetIdx = size_t(targetTris) * 3;

                    s.tmp.resize(s.localIndices.size());
                    size_t written = meshopt_simplify(
                        s.tmp.data(), s.localIndices.data(), s.localIndices.size(),
                        s.localVerts.data(), unique, positionStrideBytes,
                        targetIdx, error);

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

} // namespace SFW
