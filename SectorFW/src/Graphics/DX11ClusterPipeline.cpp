// ClusterPipeline.cpp
#include "Graphics/DX11/DX11ClusterPipeline.h"
#include <cassert>
#include <cstring>

namespace SFW::Graphics
{

    // 便利: UAV カウンタ付 StructuredBuffer を作る
    static void CreateAppendUintBuffer(ID3D11Device* dev, uint32_t maxCount,
        ID3D11Buffer** outBuf, ID3D11UnorderedAccessView** outUAV, ID3D11ShaderResourceView** outSRV)
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(uint32_t);
        bd.ByteWidth = maxCount * sizeof(uint32_t);

        ComPtr<ID3D11Buffer> buf;
        dev->CreateBuffer(&bd, nullptr, &buf);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
        uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Format = DXGI_FORMAT_UNKNOWN;
        uavd.Buffer.NumElements = maxCount;
        uavd.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND; // ← 重要：Append/Consume + Counter
        ComPtr<ID3D11UnorderedAccessView> uav;
        dev->CreateUnorderedAccessView(buf.Get(), &uavd, &uav);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Format = DXGI_FORMAT_UNKNOWN;
        srvd.Buffer.ElementWidth = maxCount;
        ComPtr<ID3D11ShaderResourceView> srv;
        dev->CreateShaderResourceView(buf.Get(), &srvd, &srv);

        *outBuf = buf.Detach();
        *outUAV = uav.Detach();
        *outSRV = srv.Detach();
    }

    void ClusterPipeline::Create(ID3D11Device* dev,
        const std::vector<ClusterInfoCPU>& clusters,
        uint32_t vertexStride,
        const void* vbData, size_t vbBytes,
        const uint32_t* ibData, size_t ibBytes,
        const std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>& bucketDefs)
    {
        // 1) ClusterInfo
        {
            D3D11_BUFFER_DESC bd{};
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = UINT(clusters.size() * sizeof(ClusterInfoCPU));
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(ClusterInfoCPU);

            D3D11_SUBRESOURCE_DATA init{ clusters.data(), 0, 0 };
            dev->CreateBuffer(&bd, &init, &clusterInfoBuf);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.Buffer.ElementWidth = UINT(clusters.size());
            dev->CreateShaderResourceView(clusterInfoBuf.Get(), &srvd, &clusterInfoSRV);
        }

        // 2) VB/IB
        {
            D3D11_BUFFER_DESC vbd{};
            vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vbd.Usage = D3D11_USAGE_IMMUTABLE;
            vbd.ByteWidth = UINT(vbBytes);
            D3D11_SUBRESOURCE_DATA ssv{ vbData, 0, 0 };
            dev->CreateBuffer(&vbd, &ssv, &vb);

            D3D11_BUFFER_DESC ibd{};
            ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            ibd.Usage = D3D11_USAGE_IMMUTABLE;
            ibd.ByteWidth = UINT(ibBytes);
            D3D11_SUBRESOURCE_DATA ssi{ ibData, 0, 0 };
            dev->CreateBuffer(&ibd, &ssi, &ib);
        }

        // 3) バケツ：AppendBuffer + IndirectArgs
        buckets.clear();
        buckets.resize(kMaxBuckets);
        // 事前に (bucketId -> (indexCount, startIndex)) を登録
        for (auto [bid, icount, istart] : bucketDefs)
        {
            Bucket& b = buckets[bid];
            if (!b.visibleBuffer) {
                CreateAppendUintBuffer(dev, 1u << 20, &b.visibleBuffer, &b.visibleUAV, &b.visibleSRV);
                // Indirect Args
                D3D11_BUFFER_DESC ad{};
                ad.BindFlags = D3D11_BIND_UNORDERED_ACCESS; // ByteAddress で書く場合は UAV にもできる
                ad.Usage = D3D11_USAGE_DEFAULT;
                ad.ByteWidth = sizeof(uint32_t) * 5;
                ad.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                dev->CreateBuffer(&ad, nullptr, &b.indirectArgs);
            }
            b.indexCountPerInstance = icount;
            b.startIndexLocation = istart;
            b.baseVertexLocation = 0;
            b.startInstanceLocation = 0;
        }

        // 4) シェーダ・レイアウト・CB は省略（あなたのビルドに合わせて作成）
    }

    void ClusterPipeline::RunCullingAndDraw(ID3D11DeviceContext* ctx,
        const float viewProj[16], float viewportW, float viewportH, float projScale,
        float tauIn, float tauOut, bool useMOC)
    {
        // --- CB 更新（ViewCB, LodCB, OcclCB）---
        struct ViewCB { float m[16]; float vw, vh, ps, pad; } v{};
        memcpy(v.m, viewProj, sizeof(float) * 16); v.vw = viewportW; v.vh = viewportH; v.ps = projScale;
        ctx->UpdateSubresource(cbView.Get(), 0, nullptr, &v, 0, 0);

        struct LodCB { float tin, tout, bias, pad; } l{ tauIn, tauOut, 0.0f, 0.0f };
        ctx->UpdateSubresource(cbLod.Get(), 0, nullptr, &l, 0, 0);

        struct OcclCB { uint32_t useMoc; uint32_t pad[3]; } o{ useMOC ? 1u : 0u,{0,0,0} };
        ctx->UpdateSubresource(cbOccl.Get(), 0, nullptr, &o, 0, 0);

        // --- カウンタを 0 にリセット（Append の Counter）---
        // Counter の初期化は UAV の "initial counts" で可能。D3D11.1 の SetUnorderedAccessViews で -1 指定等でも。
        // 互換性重視なら、専用の "Counter Reset" バッファを CopyStructureCount で 0 を書く手もある。
        UINT zero[4] = { 0,0,0,0 };
        for (auto& b : buckets) {
            if (!b.visibleUAV) continue;
            ctx->ClearUnorderedAccessViewUint(b.visibleUAV.Get(), zero);
        }

        // --- CS バインド ---
        ID3D11UnorderedAccessView* uavs[ClusterPipeline::kMaxBuckets] = {};
        UINT initialCounts[ClusterPipeline::kMaxBuckets] = {}; // 0: カウンタ初期化
        for (size_t i = 0; i < buckets.size(); ++i)
            uavs[i] = buckets[i].visibleUAV.Get();

        ctx->CSSetShader(csCull.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[1] = { clusterInfoSRV.Get() };
        ctx->CSSetShaderResources(0, 1, srvs);
        ctx->CSSetUnorderedAccessViews(0, (UINT)buckets.size(), uavs, initialCounts);
        ID3D11Buffer* cbs[3] = { cbView.Get(), cbLod.Get(), cbOccl.Get() };
        ctx->CSSetConstantBuffers(0, 3, cbs);

        // --- Dispatch ---
        // スレッド 64 × ceil(N/64)
        // ここではクラスター総数は SRV 長から CS 側で読ませる実装なので、外からは “充分大きめ” を投げても OK。
        // もしくは CPU 側で clusters.size() を保持しておき、DispatchX = (N+63)/64 とする。
        uint32_t clusterCount = 0; // ※呼び出し側で覚えておく or 別 SRV で渡す
        // 仮: 100000 クラスター
        clusterCount = 100000;
        uint32_t dispatchX = (clusterCount + 63u) / 64u;
        ctx->Dispatch(dispatchX, 1, 1);

        // UAV/ SRV を解放
        ID3D11UnorderedAccessView* nullUAVs[ClusterPipeline::kMaxBuckets] = {};
        ID3D11ShaderResourceView* nullSRVs[1] = {};
        ctx->CSSetUnorderedAccessViews(0, (UINT)buckets.size(), nullUAVs, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRVs);
        ctx->CSSetShader(nullptr, nullptr, 0);

        // --- 各バケツの Append カウント → 間接引数へ ---
        // D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS
        // {UINT IndexCountPerInstance; UINT InstanceCount; UINT StartIndexLocation; INT BaseVertexLocation; UINT StartInstanceLocation;}
        for (auto& b : buckets)
        {
            if (!b.visibleUAV || !b.indirectArgs) continue;

            // まず args を初期化
            struct Args { UINT IndexCountPerInstance, InstanceCount, StartIndexLocation; INT BaseVertexLocation; UINT StartInstanceLocation; }
            args = { b.indexCountPerInstance, 0u, b.startIndexLocation, b.baseVertexLocation, b.startInstanceLocation };
            ctx->UpdateSubresource(b.indirectArgs.Get(), 0, nullptr, &args, 0, 0);

            // AppendBuffer の要素数を InstanceCount にコピー
            // InstanceCount はバッファオフセット 4 バイト目（= indexCount の次）
            // ※ D3D11 では CopyStructureCount(dstBuffer, dstByteOffset, srcUAV)
            ctx->CopyStructureCount(b.indirectArgs.Get(), /*dstByteOffset=*/4, b.visibleUAV.Get());
        }

        // --- 描画 ---
        UINT stride = /*頂点ストライド*/  sizeof(float) * 8;
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
        ctx->IASetIndexBuffer(ib.Get(), ibFormat, 0);
        ctx->IASetInputLayout(layout.Get());
        ctx->VSSetShader(vsTerrain.Get(), nullptr, 0);
        ctx->PSSetShader(psTerrain.Get(), nullptr, 0);

        // 共有 SRV（クラスター表）
        ID3D11ShaderResourceView* vsSRVsShared[1] = { clusterInfoSRV.Get() };
        ctx->VSSetShaderResources(1, 1, vsSRVsShared); // t1 としてバインド（VS 側）

        // バケツごとに可視リスト（SRV）を差し替えて 1 回の間接描画
        for (auto& b : buckets)
        {
            if (!b.indirectArgs || !b.visibleSRV) continue;

            // VS t0 に gVisibleList
            ID3D11ShaderResourceView* vsSRVs[1] = { b.visibleSRV.Get() };
            ctx->VSSetShaderResources(0, 1, vsSRVs);

            ctx->DrawIndexedInstancedIndirect(b.indirectArgs.Get(), 0);

            // アンバインド
            ID3D11ShaderResourceView* null1[1] = { nullptr };
            ctx->VSSetShaderResources(0, 1, null1);
        }

        // 共有 SRV のアンバインド
        ID3D11ShaderResourceView* null1[1] = { nullptr };
        ctx->VSSetShaderResources(1, 1, null1);
    }
}