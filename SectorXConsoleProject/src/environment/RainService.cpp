#include <SectorFW/Util/convert_string.h>
#include <SectorFW/Debug/message.h>

#include "RainService.h"
#include "../graphics/D3D11Helpers.h"

#ifdef _DEBUG
float gDebugSpawnRadius = 80.0f;
float gDebugGravity = 9.8f;
float gDebugHeightOffset = 50.0f;
float gDebugRainAddSize = 0.02f;
float gDebugLife = 5.0f;

float gDebugRainBaseLength = 0.1f;
float gDebugRainBaseWidth = 0.025f;

float gDebugRainSpeedToLength = 0.02f;
float gDebugAlpha = 0.5f;
float gDebugLifeFade = 0.18f;

#endif


RainService::RainService(
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
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(SpawnCB);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    pDevice->CreateBuffer(&desc, nullptr, m_spawnCB.GetAddressOf());

    desc.ByteWidth = sizeof(UpdateCB);
    pDevice->CreateBuffer(&desc, nullptr, m_updateCB.GetAddressOf());

    desc.ByteWidth = sizeof(RenderCB);
    pDevice->CreateBuffer(&desc, nullptr, m_renderCB.GetAddressOf());

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
    hr = pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_rainVS);
    assert(SUCCEEDED(hr) && "Failed to create vertex shader.");

    ComPtr<ID3DBlob> psBlob;
    hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf());
#ifdef _DEBUG
    msgPath = SFW::WCharToUtf8_portable(psPath);
    DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
    hr = pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_rainPS);
    assert(SUCCEEDED(hr) && "Failed to create pixel shader.");

    m_particlePool.Create(pDevice);

    // FreeList初期化
    {
        struct InitCB
        {
            uint32_t gMaxParticles = RainParticlePool::MaxParticles;
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

	BIND_DEBUG_SLIDER_FLOAT("Rain", "spawnRadius", &gDebugSpawnRadius, 0.0f, 100.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "gravity", &gDebugGravity, 0.0f, 20.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "heightOffset", &gDebugHeightOffset, 0.0f, 200.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "addSize", &gDebugRainAddSize, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "life", &gDebugLife, 0.1f, 20.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "baseLength", &gDebugRainBaseLength, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "baseWidth", &gDebugRainBaseWidth, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speedToLength", &gDebugRainSpeedToLength, 0.0f, 0.1f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "alpha", &gDebugAlpha, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "lifeFade", &gDebugLifeFade, 0.0f, 1.0f, 0.01f);

}

void RainService::Commit(double deltaTime)
{
    Graphics::DX11::BufferUpdateDesc updateDesc{};

    auto& spawnBuf = m_cpuSpawnBuffer[currentSlot];
    auto& updateBuf = m_cpuUpdateBuffer[currentSlot];
    auto& renderBuf = m_cpuRenderBuffer[currentSlot];

    {
        std::lock_guard lock(bufMutex);

        spawnBuf.gTime = m_elapsedTime;

        updateBuf.gDt = static_cast<float>(deltaTime);
        updateBuf.gTime = m_elapsedTime;

#ifdef _DEBUG
		spawnBuf.gSpawnRadius = gDebugSpawnRadius;
		spawnBuf.gHeightOffset = gDebugHeightOffset;
		spawnBuf.gAddSize = gDebugRainAddSize;
		spawnBuf.gLife = gDebugLife;
        updateBuf.gGravity = gDebugGravity;
		updateBuf.gSpawnRadius = gDebugSpawnRadius;
		renderBuf.gBaseLength = gDebugRainBaseLength;
		renderBuf.gBaseWidth = gDebugRainBaseWidth;
		renderBuf.gSpeedToLength = gDebugRainSpeedToLength;
		renderBuf.gAlpha = gDebugAlpha;
		renderBuf.gLifeFade = gDebugLifeFade;
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

    updateDesc.buffer = m_renderCB.Get();
    updateDesc.size = sizeof(RenderCB);
    updateDesc.data = &renderBuf;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);
}

void RainService::SpawnDrawParticles(
    ID3D11DeviceContext* ctx, RainParticlePool::TiledLightData* lightData)
{
    m_particlePool.Spawn(
        ctx,
		m_spawnCS.Get(),
		m_updateCS.Get(),
		m_argsCS.Get(),
		m_spawnCB.Get(),
		m_updateCB.Get(),
		m_rainVS.Get(),
		m_rainPS.Get(),
		m_renderCB.Get(),
        lightData,
        m_spawnPerFrame
        );
}
