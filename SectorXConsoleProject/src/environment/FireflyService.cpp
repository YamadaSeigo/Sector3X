#include "FireflyService.h"
#include <SectorFW/Graphics/PointLightService.h>
#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>
#include "LeafService.h"


void CreateFireflyVolumeBuffer(
    ID3D11Device* dev,
    ID3D11Buffer** outBuf,
    ID3D11ShaderResourceView** outSRV)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(FireflyVolumeGPU) * FireflyService::MaxVolumes;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.StructureByteStride = sizeof(FireflyVolumeGPU);
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    dev->CreateBuffer(&desc, nullptr, outBuf);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.NumElements = FireflyService::MaxVolumes;

	if (!*outBuf) return;

    dev->CreateShaderResourceView(*outBuf, &srv, outSRV);
}

#ifdef _DEBUG
float gDebugFireflyAddSize = 0.02f;
float gDebugFireflyBaseSize = 0.1f;
#endif

FireflyService::FireflyService(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
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
    CreateFireflyVolumeBuffer(
        pDevice,
        &buf,
        &srv);
    m_volumeBuffer.Attach(buf);
	m_volumeSRV.Attach(srv);

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
    hr = pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_fireflyVS);
    assert(SUCCEEDED(hr) && "Failed to create vertex shader.");

    ComPtr<ID3DBlob> psBlob;
    hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf());
#ifdef _DEBUG
    msgPath = SFW::WCharToUtf8_portable(psPath);
    DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
    hr = pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_fireflyPS);
    assert(SUCCEEDED(hr) && "Failed to create pixel shader.");

    m_particlePool.Create(pDevice);

	// FreeList初期化
    {
        struct InitCB
        {
            uint32_t gMaxParticles = FireflyParticlePool::MaxParticles;
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

    // PointLight : RWStructuredBuffer<CPULightData>
    m_pointLight = CreateStructuredBufferSRVUAV(
        pDevice,
        sizeof(Graphics::GpuPointLight),
        FireflyParticlePool::MaxPointLight,
        true, true,
        0,
        D3D11_USAGE_DEFAULT,
        0);

    D3D11_BUFFER_DESC sdesc{};
    sdesc.ByteWidth = 4;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;
    sdesc.StructureByteStride = 0;

    for (int i = 0; i < Graphics::RENDER_BUFFER_COUNT; ++i)
    {
        HRESULT hr = pDevice->CreateBuffer(&sdesc, nullptr, &m_stagingCountBuf[i]);
        assert (SUCCEEDED(hr));
    }

#ifdef _DEBUG
	BIND_DEBUG_SLIDER_FLOAT("Firefly", "addSize", &gDebugFireflyAddSize, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Firefly", "baseSize", &gDebugFireflyBaseSize, 0.01f, 1.0f, 0.001f);
#endif
}

uint32_t FireflyService::AllocateSlot(uint32_t volumeUID)
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

void FireflyService::ReleaseUnusedSlots()
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

void FireflyService::PushActiveVolume(uint32_t volumeUID,
    const FireflyVolumeGPU& src)
{
    uint32_t slot = AllocateSlot(volumeUID);
    if (slot == UINT32_MAX)
        return;

    FireflyVolumeGPU v = src;
    v.volumeSlot = slot;

    m_activeVolumes.push_back(v);
}

void FireflyService::Commit(double deltaTime)
{
    auto activeSize = static_cast<uint32_t>(m_activeVolumes.size());
	m_activeVolumeCount[currentSlot] = activeSize;

  /*  if (activeSize == 0)
        return;*/

	Graphics::DX11::BufferUpdateDesc updateDesc{};
	updateDesc.buffer = m_volumeBuffer;
	updateDesc.size = sizeof(FireflyVolumeGPU) * activeSize;

    FireflyVolumeGPU* bufferData = new FireflyVolumeGPU[activeSize];
	std::memcpy(bufferData, m_activeVolumes.data(), sizeof(FireflyVolumeGPU) * activeSize);
	updateDesc.data = bufferData;
    updateDesc.isDelete = true;
    updateDesc.isArray = true;
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
		spawnBuf.gAddSizeScale = gDebugFireflyAddSize;
		camBuf.gSize = gDebugFireflyBaseSize;
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

void FireflyService::SpawnParticles(ID3D11DeviceContext* ctx, ComPtr<ID3D11ShaderResourceView>& heightMap, ComPtr<ID3D11Buffer>& terrainCB, uint32_t slot)
{
	m_particlePool.Spawn(
        ctx, m_spawnCS.Get(),
        m_updateCS.Get(),
        m_argsCS.Get(),
        m_volumeSRV.Get(),
        heightMap.Get(),
        m_pointLight.uav.Get(),
        m_spawnCB.Get(),
		terrainCB.Get(),
        m_updateCB.Get(),
        m_stagingCountBuf[currentSlot].Get(),
        m_fireflyVS.Get(),
		m_fireflyPS.Get(),
		m_cameraCB.Get(),
        m_activeVolumeCount[slot]
    );
}

