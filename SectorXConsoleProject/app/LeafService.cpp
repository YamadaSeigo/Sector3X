#include "LeafService.h"
#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>

static uint32_t Hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static float Rand01(uint32_t& s)
{
    s = Hash(s);
    return (s & 0x00FFFFFFu) / 16777216.0f;
}

static float RandRange(uint32_t& s, float a, float b)
{
    return a + (b - a) * Rand01(s);
}

void CreateLeafVolumeBuffer(
    ID3D11Device* dev,
    ID3D11Buffer** outBuf,
    ID3D11ShaderResourceView** outSRV)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(LeafVolumeGPU) * LeafService::MaxVolumes;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.StructureByteStride = sizeof(LeafVolumeGPU);
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    dev->CreateBuffer(&desc, nullptr, outBuf);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.NumElements = LeafService::MaxVolumes;

    if (!*outBuf) return;

    dev->CreateShaderResourceView(*outBuf, &srv, outSRV);
}

HRESULT CreateLeafBuffer(
    ID3D11Device* dev,
    ID3D11Buffer** outBuf,
    ID3D11ShaderResourceView** outSRV,
    uint32_t size,
    uint32_t totalCount
)
{
    if (!dev || !outBuf || !outSRV) return E_INVALIDARG;

    *outBuf = nullptr;
    *outSRV = nullptr;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size * totalCount;
    desc.Usage = D3D11_USAGE_DYNAMIC;                    // 毎フレーム更新ならOK
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = size;

    HRESULT hr = dev->CreateBuffer(&desc, nullptr, outBuf);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = totalCount;

    hr = dev->CreateShaderResourceView(*outBuf, &srv, outSRV);
    if (FAILED(hr))
    {
        (*outBuf)->Release();
        *outBuf = nullptr;
        return hr;
    }

    return S_OK;
}


#ifdef _DEBUG
float gDebugLeafAddSize = 0.02f;
float gDebugLeafBaseSize = 0.1f;
float gDebugLeafLaneMax = 1.2f;
float gDebugLeafRadialMax = 0.25f;
#endif

LeafService::LeafService(
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    Graphics::DX11::BufferManager* bufferMgr,
    const wchar_t* csInitFreeListPath,
    const wchar_t* csSpawnPath,
    const wchar_t* csUpdatePath,
    const wchar_t* csArgsPath,
    const wchar_t* vsPath,
    const wchar_t* psPath)
    : m_bufferMgr(bufferMgr)
{
    // ボリュームバッファ作成
    ID3D11Buffer* buf = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    CreateLeafVolumeBuffer(
        pDevice,
        &buf,
        &srv);
    m_volumeBuffer.Attach(buf);
    m_volumeSRV.Attach(srv);

    CreateLeafBuffer(
        pDevice,
        &buf,
        &srv,
        sizeof(GuideCurve),
        TotalGuideCurves
    );
	m_guideCurveBuffer.Attach(buf);
    m_guideCurveSRV.Attach(srv);

    CreateLeafBuffer(
        pDevice,
        &buf,
        &srv,
        sizeof(Clump),
        TotalClumps
    );
    m_clumpBuffer.Attach(buf);
    m_clumpSRV.Attach(srv);

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(SpawnCB);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    pDevice->CreateBuffer(&desc, nullptr, m_spawnCB.GetAddressOf());

    desc.ByteWidth = sizeof(UpdateCB);
    pDevice->CreateBuffer(&desc, nullptr, m_updateCB.GetAddressOf());

    desc.ByteWidth = sizeof(CameraCB);
    pDevice->CreateBuffer(&desc, nullptr, m_cameraCB.GetAddressOf());

    auto compileShader = [&](const wchar_t* path, ComPtr<ID3D11ComputeShader>& outCS)
        {
            ComPtr<ID3DBlob> csBlob;
            HRESULT hr = D3DReadFileToBlob(path, csBlob.GetAddressOf());
#ifdef _DEBUG
            std::string msgPath = SFW::WCharToUtf8_portable(path);
            DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
            hr = pDevice->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &outCS);
            assert(SUCCEEDED(hr) && "Failed to create compute shader.");
        };

    // コンピュートシェーダー作成
    compileShader(csInitFreeListPath, m_initFreeListCS);
    compileShader(csSpawnPath, m_spawnCS);
    compileShader(csUpdatePath, m_updateCS);
    compileShader(csArgsPath, m_argsCS);

    ComPtr<ID3DBlob> vsBlob;
    HRESULT hr = D3DReadFileToBlob(vsPath, vsBlob.GetAddressOf());
#ifdef _DEBUG
    std::string msgPath = SFW::WCharToUtf8_portable(vsPath);
    DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
    hr = pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_leafVS);
    assert(SUCCEEDED(hr) && "Failed to create vertex shader.");

    ComPtr<ID3DBlob> psBlob;
    hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf());
#ifdef _DEBUG
    msgPath = SFW::WCharToUtf8_portable(psPath);
    DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
    hr = pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_leafPS);
    assert(SUCCEEDED(hr) && "Failed to create pixel shader.");

    m_particlePool.Create(pDevice);

    // FreeList初期化
    {
        struct InitCB
        {
            uint32_t gMaxParticles = LeafParticlePool::MaxParticles;
            uint32_t padding[3] = {};
		};

        ComPtr<ID3D11Buffer> initCB;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(SpawnCB);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = 0;

		InitCB initData{};
        D3D11_SUBRESOURCE_DATA initGPUData{};
		initGPUData.pSysMem = &initData;

		pDevice->CreateBuffer(&desc, &initGPUData, initCB.GetAddressOf());

        m_particlePool.InitFreeList(
            pContext,
            initCB.Get(),
            m_initFreeListCS.Get());
    }

	// ガイド曲線パラメータ初期化
	InitCurveParams(12345);

#ifdef _DEBUG
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "addSize", &gDebugLeafAddSize, 0.0f, 1.0f, 0.001f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "baseSize", &gDebugLeafBaseSize, 0.01f, 1.0f, 0.001f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "laneMax", &gDebugLeafLaneMax, 0.01f, 10.0f, 0.01f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "radialMax", &gDebugLeafRadialMax, 0.01f, 10.0f, 0.01f);
#endif
}

void LeafService::PushActiveVolume(uint32_t volumeUID, const LeafVolumeGPU& volume)
{
    uint32_t slot = AllocateSlot(volumeUID);
    if (slot == UINT32_MAX)
        return;

    LeafVolumeGPU v = volume;
    v.volumeSlot = slot;

    m_activeVolumes.push_back(v);
}

void LeafService::Commit(double deltaTime)
{
    auto activeSize = static_cast<uint32_t>(m_activeVolumes.size());
    m_activeVolumeCount[currentSlot] = activeSize;

    /*  if (activeSize == 0)
          return;*/

    Graphics::DX11::BufferUpdateDesc updateDesc{};
    updateDesc.buffer = m_volumeBuffer;
    updateDesc.size = sizeof(LeafVolumeGPU) * activeSize;

    LeafVolumeGPU* bufferData = new LeafVolumeGPU[activeSize];
    std::memcpy(bufferData, m_activeVolumes.data(), sizeof(LeafVolumeGPU) * activeSize);
    updateDesc.data = bufferData;
    updateDesc.isDelete = true;
    updateDesc.isArray = true;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    //動的にガイドカーブを更新
    BuildGuideCurves(m_elapsedTime);

    // GPUも更新
    updateDesc.buffer = m_guideCurveBuffer;
    updateDesc.size = sizeof(GuideCurve) * TotalGuideCurves;
    updateDesc.data = m_cpuGuideCurves;
    updateDesc.isDelete = false;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    auto& spawnBuf = m_cpuSpawnBuffer[currentSlot];
    auto& updateBuf = m_cpuUpdateBuffer[currentSlot];
    auto& camBuf = m_cpuCameraBuffer[currentSlot];

    {
        std::lock_guard lock(bufMutex);

        spawnBuf.gActiveVolumeCount = activeSize;
        spawnBuf.gTime = m_elapsedTime;

        updateBuf.gDt = static_cast<float>(deltaTime);
        updateBuf.gTime = m_elapsedTime;

        camBuf.gTime = m_elapsedTime;

#ifdef _DEBUG
        spawnBuf.gAddSizeScale = gDebugLeafAddSize;
        spawnBuf.gLaneMax = gDebugLeafLaneMax;
        spawnBuf.gRadialMax = gDebugLeafRadialMax;
        camBuf.gSize = gDebugLeafBaseSize;
#endif
    }

    updateDesc.buffer = m_spawnCB.Get();
    updateDesc.size = sizeof(SpawnCB);
    updateDesc.data = &spawnBuf;
    updateDesc.isDelete = false;
    updateDesc.isArray = false;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_updateCB.Get();
    updateDesc.size = sizeof(UpdateCB);
    updateDesc.data = &updateBuf;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_cameraCB.Get();
    updateDesc.size = sizeof(CameraCB);
    updateDesc.data = &camBuf;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);
}

void LeafService::SpawnParticles(ID3D11DeviceContext* ctx, ComPtr<ID3D11ShaderResourceView>& heightMap, ComPtr<ID3D11ShaderResourceView>& leafTex, ComPtr<ID3D11Buffer>& terrainCB, ComPtr<ID3D11Buffer>& windCB, uint32_t slot)
{
    m_particlePool.Spawn(
        ctx, m_spawnCS.Get(),
        m_updateCS.Get(),
        m_argsCS.Get(),
        m_volumeSRV.Get(),
		m_guideCurveSRV.Get(),
        heightMap.Get(),
        leafTex.Get(),
        m_spawnCB.Get(),
        terrainCB.Get(),
        windCB.Get(),
        m_updateCB.Get(),
        m_leafVS.Get(),
        m_leafPS.Get(),
        m_cameraCB.Get(),
        m_activeVolumeCount[slot]
    );
}

uint32_t LeafService::AllocateSlot(uint32_t volumeUID)
{
    if (auto it = m_uidToSlot.find(volumeUID); it != m_uidToSlot.end())
        return it->second;

    for (uint32_t i = 0; i < MaxVolumes; ++i)
    {
        if (!m_slots[i].used)
        {
            m_slots[i].used = true;
            m_slots[i].volumeUID = volumeUID;
            m_uidToSlot[volumeUID] = i;
            return i;
        }
    }

    // 上限超過（設計ミス）
    return UINT32_MAX;
}

void LeafService::ReleaseUnusedSlots()
{
    std::unordered_set<uint32_t> activeUIDs;
    for (const auto& v : m_activeVolumes)
    {
        activeUIDs.insert(v.volumeSlot);
    }
    for (uint32_t i = 0; i < MaxVolumes; ++i)
    {
        if (m_slots[i].used)
        {
            if (activeUIDs.find(m_slots[i].volumeUID) == activeUIDs.end())
            {
                // 未使用
                m_slots[i].used = false;
                m_uidToSlot.erase(m_slots[i].volumeUID);
            }
        }
    }
}

void LeafService::InitCurveParams(uint32_t baseSeed)
{
    for (uint32_t i = 0; i < TotalGuideCurves; ++i)
    {
        uint32_t s = Hash(baseSeed ^ i);

        CurveParams p{};
        p.length = RandRange(s, 8.0f, 20.0f);

        // 左右交互にすると“束”がきれい
        float side = (i & 1) ? 1.0f : -1.0f;
        p.bend = RandRange(s, 0.6f, 2.4f) * side;

        // startは小さめ、endは少し広め
        p.startOffXZ = { RandRange(s, -1.2f, 1.2f), RandRange(s, -1.2f, 1.2f) };
        p.endOffXZ = { RandRange(s, -2.8f, 2.8f), RandRange(s, -2.8f, 2.8f) };

        m_curveParams[i] = p;
    }
}

void LeafService::BuildGuideCurves(float timeSec)
{
    using namespace SFW::Math;

    for (uint32_t i = 0; i < TotalGuideCurves; ++i)
    {
        const auto& prm = m_curveParams[i];

        float L = prm.length;

        // ゆっくり揺らす（任意）
        float bend = prm.bend * (0.85f + 0.15f * std::sinf(timeSec * 0.7f + i));

        // ローカル: X=right, Y=up, Z=fwd
        Vec3f P0 = { prm.startOffXZ.x, 0.0f, prm.startOffXZ.y };
        Vec3f P3 = { prm.endOffXZ.x,   0.0f, prm.endOffXZ.y + L };

        Vec3f P1 = P0 + Vec3f{ bend, 0.0f, L * 0.33f };
        Vec3f P2 = P0 + Vec3f{ -bend, 0.0f, L * 0.66f };

        GuideCurve& c = m_cpuGuideCurves[i];
        c.p0L = P0;
        c.p1L = P1;
        c.p2L = P2;
        c.p3L = P3;

        c.lengthApprox = L; // 厳密じゃなくてもOK（ざっくりで良い）
    }
}
