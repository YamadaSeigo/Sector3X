// LeafParticlePool.cpp
#include "LeafParticlePool.h"

#include <utility> // std::swap
#include <cassert>

#ifdef _DEBUG
// デバッグ用: AliveCount をCPUで読んで確認する機能を有効化（必要なら）
#define DEBUG_READ_ALIVE_COUNT 0
#endif

#if DEBUG_READ_ALIVE_COUNT

// 4バイト（uint）をCPUで読むための staging
Microsoft::WRL::ComPtr<ID3D11Buffer> gLeafAliveCountReadback;

static void CreateLeafReadbackBuffer(ID3D11Device* dev)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 4;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    dev->CreateBuffer(&desc, nullptr, gLeafAliveCountReadback.ReleaseAndGetAddressOf());
}

static uint32_t ReadLeafAliveCount(ID3D11DeviceContext* ctx, ID3D11Buffer* src, ID3D11Buffer* staging)
{
    ctx->CopyResource(staging, src);

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &ms)) && ms.pData)
    {
        uint32_t value = *reinterpret_cast<const uint32_t*>(ms.pData);
        ctx->Unmap(staging, 0);
        return value;
    }
    return 0;
}
#endif // DEBUG_READ_ALIVE_COUNT

#ifdef _DEBUG
// Firefly と同じスタイルのデバッグスライダー用マクロ（名前だけ Leaf に変更）
#define BIND_DEBUG_LEAF_PARAM_FLOAT(var, min, max, speed) \
    REGISTER_DEBUG_SLIDER_FLOAT("Leaf", #var, m_cpuUpdateParam.var, min, max, speed, [&](float value) { \
        m_isUpdateParamDirty = true; \
        m_cpuUpdateParam.var = value; \
    })
#endif

void LeafParticlePool::Create(ID3D11Device* dev)
{
    // Particle pool: RWStructuredBuffer<LeafParticleGPU>
    m_particles = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(LeafParticleGPU),
        MaxParticles,
        true,  // SRV
        true,  // UAV
        0,     // UAV flags
        D3D11_USAGE_DEFAULT,
        0      // CPUAccessFlags
    );

    // FreeList: AppendStructuredBuffer<uint>
    m_free = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxParticles,
        false, // SRV不要
        true,  // UAV
        D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT,
        0
    );

    // AlivePing/Pong (uint)
    m_alivePing = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxParticles,
        true,  // SRV
        true,  // UAV
        D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT,
        0
    );

    m_alivePong = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxParticles,
        true,
        true,
        D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT,
        0
    );

    // VolumeCount: RWStructuredBuffer<uint>
    m_volumeCount = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxVolumeSlots,
        false, // SRV不要（UpdateCS だけでOKなら）
        true,  // UAV
        0,
        D3D11_USAGE_DEFAULT,
        0
    );

    // AliveCountRaw: 4 bytes（1 uint）
    m_aliveCountRaw = CreateRawBufferSRVUAV(
        dev,
        4,
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        true,   // SRV
        false   // UAV は不要
    );

    // DrawArgsRaw: 16 bytes（4 uint）+ DRAWINDIRECT
    m_drawArgsRaw = CreateRawBufferSRVUAV(
        dev,
        16,
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
        D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS,
        false,  // SRV不要
        true    // UAV
    );

    // 地形HeightMap用サンプラー（Firefly と同じ）
    m_heightMapSampler = CreateSamplerState(
        dev,
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_WRAP
    );

    // UpdateParam のCB
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(LeafUpdateParam);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;

    // 初期値を m_cpuUpdateParam から入れておく
    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = &m_cpuUpdateParam;

    dev->CreateBuffer(&desc, &initData, m_cbUpdateParam.ReleaseAndGetAddressOf());

#if DEBUG_READ_ALIVE_COUNT
    CreateLeafReadbackBuffer(dev);
#endif

#ifdef _DEBUG
    // Firefly と同じように GUI からパラメータをいじる場合
    BIND_DEBUG_LEAF_PARAM_FLOAT(gDamping, 0.0f, 1.0f, 0.001f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gWanderFreq, 0.0f, 10.0f, 0.01f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gWanderStrength, 0.0f, 10.0f, 0.01f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gCenterPull, 0.0f, 10.0f, 0.01f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gGroundBand, 0.0f, 100.0f, 0.1f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gGroundPull, 0.0f, 1.0f, 0.01f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(gHeightRange, 0.0f, 100.0f, 0.1f);

    BIND_DEBUG_LEAF_PARAM_FLOAT(burstStrength, 0.0f, 20.0f, 0.1f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(burstRadius, 0.0f, 20.0f, 0.1f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(burstSwirl, 0.0f, 20.0f, 0.1f);
    BIND_DEBUG_LEAF_PARAM_FLOAT(burstUp, 0.0f, 20.0f, 0.1f);

    BIND_DEBUG_LEAF_PARAM_FLOAT(gMaxSpeed, 0.0f, 20.0f, 0.1f);
#endif
}

void LeafParticlePool::InitFreeList(
    ID3D11DeviceContext* ctx,
    ID3D11Buffer* spawnCB,
    ID3D11ComputeShader* initCS)
{
    // FreeList の counter を 0 にしてから initCS で Append(i) する
    ID3D11UnorderedAccessView* uavs[1] = { m_free.uav.Get() };
    UINT initialCounts[1] = { 0 }; // counter reset
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

    ctx->CSSetShader(initCS, nullptr, 0);

    // SpawnCB を使い回してもOK（中身はインデックス初期化用）
    ctx->CSSetConstantBuffers(0, 1, &spawnCB);

    const uint32_t threads = 256;
    uint32_t groups = (MaxParticles + threads - 1) / threads;
    ctx->Dispatch(groups, 1, 1);

    // UAV解除
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    UINT dummy[1] = { 0 };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, dummy);

    ctx->CSSetShader(nullptr, nullptr, 0);
}

void LeafParticlePool::Spawn(
    ID3D11DeviceContext* ctx,
    ID3D11ComputeShader* spawnCS,
    ID3D11ComputeShader* updateCS,
    ID3D11ComputeShader* argsCS,
    ID3D11ShaderResourceView* volumeSRV,
    ID3D11ShaderResourceView* guideCurveSRV,
    ID3D11ShaderResourceView* heightMapSRV,
    ID3D11Buffer* cbSpawnData,
    ID3D11Buffer* cbTerrain,
    ID3D11Buffer* cbWind,
    ID3D11Buffer* cbUpdateData,
    ID3D11VertexShader* vs,
    ID3D11PixelShader* ps,
    ID3D11Buffer* cbCameraData,
    uint32_t activeVolumeCount)
{
    // -----------------------------
    // (0) “書き込み先” AlivePong を 0 リセット
    // -----------------------------
    {
        ID3D11UnorderedAccessView* uav = m_alivePong.uav.Get();
        UINT initCount = 0;
        // slotはどこでもOK。ここでは u1 相当の場所で一旦当ててリセットする
        ctx->CSSetUnorderedAccessViews(1, 1, &uav, &initCount);
    }

    // -----------------------------
    // (1) Spawn: AlivePong に Append（新規生成）
    // -----------------------------
    {
        // SRV
        ctx->CSSetShaderResources(0, 1, &volumeSRV);
        ctx->CSSetShaderResources(1, 1, &heightMapSRV);

        ctx->CSSetSamplers(0, 1, m_heightMapSampler.GetAddressOf());

        // UAVs: u0 particles, u1 alivePong, u2 free(consume), u3 volumeCount
        ID3D11UnorderedAccessView* uavs[4] =
        {
            m_particles.uav.Get(),     // u0
            m_alivePong.uav.Get(),     // u1 (Append)  ← Pongへ
            m_free.uav.Get(),          // u2 (Consume)
            m_volumeCount.uav.Get(),   // u3
        };
        // Pong はさっきリセット済みだが、ここで確実に 0 を指定してもOK
        UINT initialCounts[4] = { (UINT)-1, 0, (UINT)-1, (UINT)-1 };
        ctx->CSSetUnorderedAccessViews(0, 4, uavs, initialCounts);

        // CB: 0 = SpawnCB, 1 = TerrainCB
        ctx->CSSetConstantBuffers(0, 1, &cbSpawnData);
        ctx->CSSetConstantBuffers(1, 1, &cbTerrain);

        ctx->CSSetShader(spawnCS, nullptr, 0);

        uint32_t totalThreads = activeVolumeCount * MaxSpawnPerVol; // CB と一致させる
        uint32_t groups = (totalThreads + 64 - 1) / 64;             // SpawnCS: [numthreads(64,1,1)]
        if (groups > 0)
            ctx->Dispatch(groups, 1, 1);
    }

    // -----------------------------
    // (2) Update入力のために “AlivePing（前フレーム生存）” の count を取る
    // ※ Spawn では Ping を触らない（ここが重要）
    // -----------------------------
    ctx->CopyStructureCount(m_aliveCountRaw.buf.Get(), 0, m_alivePing.uav.Get());

#if DEBUG_READ_ALIVE_COUNT
    {
        uint32_t aliveCountCPU = ReadLeafAliveCount(ctx, m_aliveCountRaw.buf.Get(), gLeafAliveCountReadback.Get());
        LOG_INFO("Leaf aliveCount = %u", aliveCountCPU);
    }
#endif

    // -----------------------------
    // (3) Update: AlivePing(SRV) → AlivePong(Append)
    // -----------------------------
    {
        // t0 = volumeSRV, t1 = alivePingSRV, t2 = aliveCountRawSRV, t3 = heightMapSRV
        ID3D11ShaderResourceView* updateSrvs[5] =
        {
            volumeSRV,
            m_alivePing.srv.Get(),
            m_aliveCountRaw.srv.Get(),
            heightMapSRV,
            guideCurveSRV
        };
        ctx->CSSetShaderResources(0, _countof(updateSrvs), updateSrvs);

        // u0 = particlesUAV, u1 = alivePongUAV, u2 = freeListUAV, u3 = volumeCountUAV
        ID3D11UnorderedAccessView* updateUavs[4] =
        {
            m_particles.uav.Get(),   // u0
            m_alivePong.uav.Get(),   // u1 (Append)  ※ここは -1 で保持（0にしない）
            m_free.uav.Get(),        // u2 (Appendで返却)
            m_volumeCount.uav.Get()  // u3
        };
        // PongはSpawnで0リセット済み＋Spawnが既にAppendしたので、ここで0にするとSpawn分が消える。
        UINT updateInitialCounts[4] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1 };
        ctx->CSSetUnorderedAccessViews(0, 4, updateUavs, updateInitialCounts);

        // UpdateParamCB 更新（dirty のときだけ）
        if (m_isUpdateParamDirty)
        {
            D3D11_MAPPED_SUBRESOURCE ms{};
            HRESULT hr = ctx->Map(m_cbUpdateParam.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
            if (SUCCEEDED(hr) && ms.pData)
            {
                memcpy(ms.pData, &m_cpuUpdateParam, sizeof(LeafUpdateParam));
                ctx->Unmap(m_cbUpdateParam.Get(), 0);
                m_isUpdateParamDirty = false;
            }
        }

        ID3D11Buffer* cbBuffers[] =
        {
            cbUpdateData,           // b0: フレーム共通 UpdateCB
			cbWind,                  // b1: WindCB

			//必要なら追加
            //cbTerrain,              // b2: TerrainCB
            //m_cbUpdateParam.Get(), // b3: LeafUpdateParam,
        };
        ctx->CSSetConstantBuffers(0, _countof(cbBuffers), cbBuffers);

        ctx->CSSetShader(updateCS, nullptr, 0);

        // AlivePing の数だけ UpdateCS を回す（AliveCountRaw に前フレームの数が入っている）
        // ※ UpdateCS 側で DispatchCount を使う実装にしてもOK
        // ここでは適当に「MaxParticles 前提」で groups を決めても良い
        uint32_t groups = (MaxParticles + 256 - 1) / 256; // [numthreads(256,1,1)] 前提
        ctx->Dispatch(groups, 1, 1);

		// unbind
        ID3D11ShaderResourceView* nullSrvs[4] = { nullptr, nullptr, nullptr, nullptr };
        ctx->CSSetShaderResources(0, 4, nullSrvs);
        ID3D11UnorderedAccessView* nullUavs[4] = { nullptr, nullptr, nullptr, nullptr };
        UINT dummy[4] = { 0,0,0,0 };
        ctx->CSSetUnorderedAccessViews(0, 4, nullUavs, dummy);
		ctx->CSSetShader(nullptr, nullptr, 0);
    }

    // -----------------------------
    // (4) Ping/Pong を swap：AlivePing が “今フレーム生存” になる
    // -----------------------------
    std::swap(m_alivePing, m_alivePong);

    // -----------------------------
    // (5) 描画用に AlivePing の count を取り、ArgsCS で DrawArgs を更新
    // -----------------------------
    ctx->CopyStructureCount(m_aliveCountRaw.buf.Get(), 0, m_alivePing.uav.Get());

    {
        // ArgsCS: t0 = aliveCountRawSRV, u0 = drawArgsRawUAV
        ID3D11ShaderResourceView* argsSrv = m_aliveCountRaw.srv.Get();
        ctx->CSSetShaderResources(0, 1, &argsSrv);

        ID3D11UnorderedAccessView* argsUav = m_drawArgsRaw.uav.Get();
        UINT keep = (UINT)-1;
        ctx->CSSetUnorderedAccessViews(0, 1, &argsUav, &keep);

        ctx->CSSetShader(argsCS, nullptr, 0);
        ctx->Dispatch(1, 1, 1);

        // unbind
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ctx->CSSetShaderResources(0, 1, &nullSrv);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        UINT dummy = 0;
        ctx->CSSetUnorderedAccessViews(0, 1, &nullUav, &dummy);
        ctx->CSSetShader(nullptr, nullptr, 0);
    }

    // 6) Draw: Billboard
    // IA: 頂点バッファ無し（null）、Index無し
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // VS SRV: t0 particlesSRV, t1 alivePingSRV(=current), t2 volumeSRV
    ID3D11ShaderResourceView* vsSrvs[] =
    {
        m_particles.srv.Get(),
        m_alivePing.srv.Get(),
        volumeSRV
    };
    ctx->VSSetShaderResources(0, 3, vsSrvs);

    // CBCamera
    ctx->VSSetConstantBuffers(0, 1, &cbCameraData);

    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);

    ctx->DrawInstancedIndirect(m_drawArgsRaw.buf.Get(), 0);

    // VS SRV unbind（次のパスで競合しにくくする）
    {
        ID3D11ShaderResourceView* nullVsSrvs[3] = { nullptr, nullptr, nullptr };
        ctx->VSSetShaderResources(0, 3, nullVsSrvs);
    }
}