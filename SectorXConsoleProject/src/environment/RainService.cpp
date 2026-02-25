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

float gDebugSplashSpeed = 1.0f;

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
	Graphics::DX11::BufferCreateDesc bufDesc{};
	bufDesc.name = "RainSpawn";
	bufDesc.size = sizeof(SpawnCB);
	bufferMgr->Add(bufDesc, m_spawnCBHandle);
	bufDesc.name = "RainUpdate";
	bufDesc.size = sizeof(UpdateCB);
	bufferMgr->Add(bufDesc, m_updateCBHandle);
	bufDesc.name = "RainRender";
	bufDesc.size = sizeof(RenderCB);
	bufferMgr->Add(bufDesc, m_renderCBHandle);
    bufDesc.name = "RainSplash";
	bufDesc.size = sizeof(SplashCB);
	bufferMgr->Add(bufDesc, m_splashCBHandle);

    {
        auto readLock = bufferMgr->AcquireReadLock();
		auto bufferData = bufferMgr->GetNoLock(m_spawnCBHandle);
		m_spawnCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_updateCBHandle);
		m_updateCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_renderCBHandle);
		m_renderCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_splashCBHandle);
		m_splashCB = bufferData.buffer.Get();
    }

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

    D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = texDesc.Height = RainService::DEPTH_MAP_SIZE;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	pDevice->CreateTexture2D(&texDesc, nullptr, m_depthMap.GetAddressOf());

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	pDevice->CreateDepthStencilView(m_depthMap.Get(), &dsvDesc, m_depthMapDSV.GetAddressOf());

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	pDevice->CreateShaderResourceView(m_depthMap.Get(), &srvDesc, m_depthMapSRV.GetAddressOf());

    for(int i = 0; i < Graphics::RENDER_BUFFER_COUNT; ++i)
    {
		float invMapSize = 1.0f / static_cast<float>(RainService::DEPTH_MAP_SIZE);
		m_cpuUpdateBuffer[i].gRainInvMapSize.x = invMapSize;
		m_cpuUpdateBuffer[i].gRainInvMapSize.y = invMapSize;
    }

#ifdef _DEBUG
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
	BIND_DEBUG_SLIDER_FLOAT("Rain", "splashSpeed", &gDebugSplashSpeed, 0.0f, 5.0f, 0.01f);
#endif

}

void RainService::Commit(double deltaTime)
{
    Graphics::DX11::BufferUpdateDesc updateDesc{};

    auto& spawnBuf = m_cpuSpawnBuffer[currentSlot];
    auto& updateBuf = m_cpuUpdateBuffer[currentSlot];
    auto& renderBuf = m_cpuRenderBuffer[currentSlot];
	auto& splashBuf = m_cpuSplashBuffer[currentSlot];

    {
        std::lock_guard lock(bufMutex);

        spawnBuf.gTime = m_elapsedTime;

        updateBuf.gDt = static_cast<float>(deltaTime);
        updateBuf.gTime = m_elapsedTime;

		splashBuf.gTime = m_elapsedTime;

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
		splashBuf.gSplashSpeed = gDebugSplashSpeed;
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

	updateDesc.buffer = m_splashCB.Get();
	updateDesc.size = sizeof(SplashCB);
	updateDesc.data = &splashBuf;
	m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);
}

void RainService::ClearDepthMap(ID3D11DeviceContext* ctx)
{
    FLOAT clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	ctx->ClearDepthStencilView(m_depthMapDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void RainService::SpawnDrawParticles(
    ID3D11DeviceContext* ctx, RainParticlePool::TiledLightData* lightData)
{
    m_particlePool.Spawn(
        ctx,
		m_spawnCS.Get(),
		m_updateCS.Get(),
		m_argsCS.Get(),
		m_depthMapSRV.Get(),
		m_spawnCB.Get(),
		m_updateCB.Get(),
		m_rainVS.Get(),
		m_rainPS.Get(),
		m_renderCB.Get(),
        lightData,
        m_spawnPerFrame
        );
}

Math::Matrix4x4f RainService::MakeDepthMapViewProjNoLock() const
{
    Math::Vec3f camPos = m_cpuSpawnBuffer[currentSlot].gCamPos;
    float spawnRadius = m_cpuSpawnBuffer[currentSlot].gSpawnRadius;
	float heightOffset = m_cpuSpawnBuffer[currentSlot].gHeightOffset;

	constexpr float topMargin = 20.0f; // スポーン範囲の上に余裕を持たせるためのマージン
	constexpr float bottomMargin = 10.0f; // スポーン範囲の下に余裕を持たせるためのマージン

	Math::Vec3f eye = camPos + Math::Vec3f(0.0f, heightOffset + topMargin, 0.0f);
	Math::Vec3f at = camPos;
	Math::Vec3f up = Math::Vec3f(0.0f, 0.0f, 1.0f);
	Math::Matrix4x4f view = Math::MakeLookAtMatrixLH(eye, at, up);

	float left = -spawnRadius;
	float right = spawnRadius;
	float top = spawnRadius;
	float bottom = -spawnRadius;
	float nearZ = 0.1f;
	float farZ = heightOffset + spawnRadius + bottomMargin; // カメラの高さ + スポーン半径 + 下マージン
    Math::Matrix4x4f proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(left, right, top, bottom, nearZ, farZ);

	return proj * view;
}
