// FireflyParticlePool.cpp
#include "FireflyParticlePool.h"
#include <utility>//　swap

#ifdef _DEBUG
// デバッグ用: AliveCount をCPUで読んで確認する機能を有効化
#define DEBUG_READ_ALIVE_COUNT 0
#endif

#if DEBUG_READ_ALIVE_COUNT

// 4バイト（uint）をCPUで読むための staging
Microsoft::WRL::ComPtr<ID3D11Buffer> gAliveCountReadback;

void CreateReadbackBuffer(ID3D11Device* dev)
{
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 4;
    bd.Usage = D3D11_USAGE_STAGING;
    bd.BindFlags = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags = 0;

    HRESULT hr = dev->CreateBuffer(&bd, nullptr, gAliveCountReadback.GetAddressOf());
    // 失敗時はログ/ASSERT
}

uint32_t ReadAliveCount(
    ID3D11DeviceContext* ctx,
    ID3D11Buffer* aliveCountRawDefaultBuf,   // CopyStructureCount の出力先
    ID3D11Buffer* aliveCountReadbackStaging) // 上で作った staging
{
    // GPUのDEFAULTバッファ → CPU読み取り用 staging へコピー
    ctx->CopyResource(aliveCountReadbackStaging, aliveCountRawDefaultBuf);

    // ここで必要なら GPU完了待ち（デバッグ時のみ推奨）
    // ctx->Flush(); だけでは完了保証ではないですが、まずの切り分けには役立つことが多いです。
    // より確実には D3D11_QUERY_EVENT を使う（下に載せます）

    D3D11_MAPPED_SUBRESOURCE ms{};
    HRESULT hr = ctx->Map(aliveCountReadbackStaging, 0, D3D11_MAP_READ, 0, &ms);
    if (FAILED(hr) || !ms.pData)
        return 0;

    uint32_t value = *reinterpret_cast<const uint32_t*>(ms.pData);
    ctx->Unmap(aliveCountReadbackStaging, 0);
    return value;
}


#endif // DEBUG_READ_ALIVE_COUNT

#define BIND_DEBUG_FIREFLY_PRAM_FLOAT(var, min, max, speed)\
REGISTER_DEBUG_SLIDER_FLOAT("Firefly", #var, m_cpuUpdateParam.##var, ##min, ##max, speed, [&](float value){\
	m_isUpdateParamDirty= true; m_cpuUpdateParam.##var = value;})

void FireflyParticlePool::Create(ID3D11Device* dev)
{
    // Particle pool: RWStructuredBuffer<FireflyParticle>
    m_particles = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(FireflyParticleGPU),
        MaxParticles,
        true, true,
        0,
        D3D11_USAGE_DEFAULT,
        0);

    // FreeList: AppendStructuredBuffer<uint>
    m_free = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxParticles,
        false, true,
        D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT,
        0);

    // AlivePing/Pong (uint)
    m_alivePing = CreateStructuredBufferSRVUAV(
        dev, sizeof(uint32_t), MaxParticles,
        true, true, D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT, 0);

    m_alivePong = CreateStructuredBufferSRVUAV(
        dev, sizeof(uint32_t), MaxParticles,
        true, true, D3D11_BUFFER_UAV_FLAG_APPEND,
        D3D11_USAGE_DEFAULT, 0);

    // VolumeCount: RWStructuredBuffer<uint>
    m_volumeCount = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        MaxVolumeSlots,
        false, true,
        0,
        D3D11_USAGE_DEFAULT,
        0);

    // AliveCountRaw: 4 bytes（1 uint）
    m_aliveCountRaw = CreateRawBufferSRVUAV(
        dev, 4,
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        true, false);

    // DrawArgsRaw: 16 bytes（4 uint）+ DRAWINDIRECT
    m_drawArgsRaw = CreateRawBufferSRVUAV(
        dev, 16,
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS,
        false, true);

    m_pointLightCount = CreateRawBufferSRVUAV(
        dev, 4,
        D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        false, true
    );

    m_linearSampler = CreateSamplerState(dev, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP);

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(FireflyUpdatePram);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = &m_cpuUpdateParam;

    dev->CreateBuffer(&desc, &initData, m_cbUpdateParam.GetAddressOf());

#if DEBUG_READ_ALIVE_COUNT
    CreateReadbackBuffer(dev);
#endif

#ifdef _DEBUG

	BIND_DEBUG_FIREFLY_PRAM_FLOAT(gDamping, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_FIREFLY_PRAM_FLOAT(gWanderFreq, 0.0f, 10.0f, 0.01f);
    BIND_DEBUG_FIREFLY_PRAM_FLOAT(gWanderStrength, 0.0f, 10.0f, 0.01f);
	BIND_DEBUG_FIREFLY_PRAM_FLOAT(gCenterPull, 0.0f, 10.0f, 0.01f);
    BIND_DEBUG_FIREFLY_PRAM_FLOAT(gGroundBand, 0.0f, 50.0f, 0.01f);
    BIND_DEBUG_FIREFLY_PRAM_FLOAT(gGroundPull, 0.0f, 10.0f, 0.01f);
	BIND_DEBUG_FIREFLY_PRAM_FLOAT(gHeightRange, 0.0f, 50.0f, 0.01f);

    BIND_DEBUG_FIREFLY_PRAM_FLOAT(burstStrength, 0.0f, 100.0f, 0.01f);
    BIND_DEBUG_FIREFLY_PRAM_FLOAT(burstRadius, 0.0f, 20.0f, 0.01f);
    BIND_DEBUG_FIREFLY_PRAM_FLOAT(burstSwirl, 0.0f, 50.0f, 0.01f);
	BIND_DEBUG_FIREFLY_PRAM_FLOAT(burstUp, 0.0f, 50.0f, 0.01f);

    BIND_DEBUG_FIREFLY_PRAM_FLOAT(gMaxSpeed, 0.0f, 50.0f, 0.01f);

#endif
}

void FireflyParticlePool::InitFreeList(ID3D11DeviceContext* ctx, ID3D11Buffer* initCB, ID3D11ComputeShader* initCS)
{
    // FreeList の counter を 0 にしてから initCS で Append(i) する
    ID3D11UnorderedAccessView* uavs[1] = { m_free.uav.Get() };
    UINT initialCounts[1] = { 0 }; // counter reset
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

    ctx->CSSetShader(initCS, nullptr, 0);

    ctx->CSSetConstantBuffers(0, 1, &initCB);

    const uint32_t threads = 256;
    uint32_t groups = (MaxParticles + threads - 1) / threads;
    ctx->Dispatch(groups, 1, 1);

    // UAV解除（お作法）
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    UINT dummy[1] = { 0 };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, dummy);
}

void FireflyParticlePool::Spawn(
    ID3D11DeviceContext* ctx,
    ID3D11ComputeShader* spawnCS,
    ID3D11ComputeShader* updateCS,
    ID3D11ComputeShader* argsCS,
    ID3D11ShaderResourceView* volumeSRV,
	ID3D11ShaderResourceView* heightMapSRV,
    ID3D11UnorderedAccessView* pointLightUAV,
	ID3D11Buffer* cbSpawnData,
    ID3D11Buffer* cbTerrain,
	ID3D11Buffer* cbUpdateData,
    ID3D11Buffer* stagingBuf,
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

		ctx->CSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

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

        ctx->CSSetConstantBuffers(0, 1, &cbSpawnData);
		ctx->CSSetConstantBuffers(1, 1, &cbTerrain);

        ctx->CSSetShader(spawnCS, nullptr, 0);

        uint32_t totalThreads = activeVolumeCount * MaxSpawnPerVol; // CBと一致させる
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
    // デバッグ読み取り
    uint32_t aliveCountCPU = ReadAliveCount(ctx, m_aliveCountRaw.buf.Get(), gAliveCountReadback.Get());
    LOG_INFO("aliveCount = %u", aliveCountCPU);
#endif

    // UpdateCS bindings:
    // t0 = volumeSRV (※slot参照できる形のvolume bufferが必要)
    // t1 = alivePingSRV (前フレームの生存)
    // t2 = aliveCountRawSRV (前フレームの生存数)
    // u0 = particlesUAV
    // u1 = alivePongUAV (append)  ← Spawnで入れたPongへさらに詰める
    // u2 = freeListUAV (append返却)
    // u3 = volumeCountUAV

    // -----------------------------
    // (3) Update: AlivePing(SRV) → AlivePong(Append)
    // -----------------------------
    {
        uint32_t zero = 0;
        ctx->UpdateSubresource(m_pointLightCount.buf.Get(), 0, nullptr, &zero, 0, 0);

        ID3D11ShaderResourceView* updateSrvs[4] = {
        volumeSRV,
        m_alivePing.srv.Get(),
        m_aliveCountRaw.srv.Get(),
        heightMapSRV
        };
        ctx->CSSetShaderResources(0, 4, updateSrvs);

        ID3D11UnorderedAccessView* updateUavs[6] = {
        m_particles.uav.Get(),      // u0
        m_alivePong.uav.Get(),      // u1 (Append)  ※ここは -1 で保持（0にしない）
        m_free.uav.Get(),           // u2 (Appendで返却)
        m_volumeCount.uav.Get(),    // u3
        pointLightUAV,              // u4
        m_pointLightCount.uav.Get() // u5
        };
        // PongはSpawnで0リセット済み＋Spawnが既にAppendしたので、ここで0にするとSpawn分が消える。
        UINT updateInitialCounts[6] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)0, (UINT)0};
        ctx->CSSetUnorderedAccessViews(0, 6, updateUavs, updateInitialCounts);

        if (m_isUpdateParamDirty)
        {
            D3D11_MAPPED_SUBRESOURCE ms{};
            HRESULT hr = ctx->Map(m_cbUpdateParam.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
            if (SUCCEEDED(hr) && ms.pData)
            {
                memcpy(ms.pData, &m_cpuUpdateParam, sizeof(FireflyUpdatePram));
                ctx->Unmap(m_cbUpdateParam.Get(), 0);
                m_isUpdateParamDirty = false;
			}
        }

		ID3D11Buffer* cbBuffers[] = { cbUpdateData, cbTerrain, m_cbUpdateParam.Get()};

        ctx->CSSetConstantBuffers(0, 3, cbBuffers);

        ctx->CSSetShader(updateCS, nullptr, 0);

        // まずは固定最大dispatchでもOK（shader側で aliveCount を見て弾く）
        uint32_t updateGroups = (MaxParticles + 256 - 1) / 256; // UpdateCS: [numthreads(256,1,1)]
        if (updateGroups > 0)
            ctx->Dispatch(updateGroups, 1, 1);

        ctx->CopyResource(stagingBuf, m_pointLightCount.buf.Get());
    }

    // CSバインド解除（後続のCopyStructureCount/Drawで事故りにくくする）
    {
        ID3D11ShaderResourceView* nullSrvs[3] = { nullptr, nullptr, nullptr };
        ctx->CSSetShaderResources(0, 3, nullSrvs);
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
    ID3D11ShaderResourceView* vsSrvs[] = {
        m_particles.srv.Get(),
        m_alivePing.srv.Get(),
        volumeSRV
    };
    ctx->VSSetShaderResources(0, 3, vsSrvs);

    // VS/PS set shaders + CBCamera
    // Blend additive 推奨

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
