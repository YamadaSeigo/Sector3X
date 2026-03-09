#include <SectorFW/Util/convert_string.h>
#include <SectorFW/Debug/message.h>

#include "RainService.h"
#include "../app/appconfig.h"
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
float gDebugLifeFade = 0.4f;

float gDebugWetStrength = 1.0f;
float gDebugWetMinNdotUp = 0.2f;

float gDebugWetEdgeThreshold = 2.0f;
float gDebugWetEdgeSharpness = 0.2f;
float gDebugWetNearThickZ = 30.0f;
float gDebugWetFarThickZ = 46.0f;
float gDebugWetUpMin = 0.2f;
float gDebugWetUpMax = 0.8f;
float gDebugWetFarFade = 0.02f;

float gDebugSpeckleCellSize = 0.01f;
float gDebugSpeckleDensity = 5.0f;
float gDebugSpeckleAmount = 0.15f;
float gDebugSpeckleTimeHz = 20.0f;

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
    const wchar_t* psPath,
    const wchar_t* csWetnessScrollPath,
    const wchar_t* csWetnessUpdatePath)
	: m_bufferMgr(bufferMgr)
{
	Graphics::DX11::BufferCreateDesc bufDesc{};
	bufDesc.name = "RainSpawn";
	bufDesc.size = sizeof(SpawnCB);
	bufferMgr->Add(bufDesc, m_spawnCBHandle);
	bufDesc.name = "RainUpdate";
	bufDesc.size = sizeof(UpdateCB);
	bufferMgr->Add(bufDesc, m_updateCBHandle);
	bufDesc.name = "RainMatrix";
	bufDesc.size = sizeof(MatrixCB);
	bufferMgr->Add(bufDesc, m_matrixCBHandle);
	bufDesc.name = "RainRender";
	bufDesc.size = sizeof(RenderCB);
	bufferMgr->Add(bufDesc, m_renderCBHandle);
    bufDesc.name = "RainSplash";
	bufDesc.size = sizeof(WetnessCB);
	bufferMgr->Add(bufDesc, m_wetnessCBHandle);
    bufDesc.name = "RainScroll";
    bufDesc.size = sizeof(WetnessScrollCB);
    bufferMgr->Add(bufDesc, m_wetScrollCBHandle);
    bufDesc.name = "RainWetnessUpdate";
    bufDesc.size = sizeof(WetnessUpdateCB);
    bufferMgr->Add(bufDesc, m_wetUpdateCBHandle);

    {
        auto readLock = bufferMgr->AcquireReadLock();
		auto bufferData = bufferMgr->GetNoLock(m_spawnCBHandle);
		m_spawnCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_updateCBHandle);
		m_updateCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_matrixCBHandle);
		m_matrixCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_renderCBHandle);
		m_renderCB = bufferData.buffer.Get();
		bufferData = bufferMgr->GetNoLock(m_wetnessCBHandle);
		m_wetnessCB = bufferData.buffer.Get();
        bufferData = bufferMgr->GetNoLock(m_wetScrollCBHandle);
        m_wetScrollCB = bufferData.buffer.Get();
        bufferData = bufferMgr->GetNoLock(m_wetUpdateCBHandle);
        m_wetUpdateCB = bufferData.buffer.Get();
    }

    auto compileComputeShader = [&](const wchar_t* path, ComPtr<ID3D11ComputeShader>& outCS)
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
    compileComputeShader(csInitFreeListPath, m_initFreeListCS);
    compileComputeShader(csSpawnPath, m_spawnCS);
    compileComputeShader(csUpdatePath, m_updateCS);
    compileComputeShader(csArgsPath, m_argsCS);
    compileComputeShader(csWetnessScrollPath, m_wetScrollCopyCS);
    compileComputeShader(csWetnessUpdatePath, m_wetUpdateCS);

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

    // depth: 0(near) -> 1(far)
    float n = 0.1f;
    float f = 1500.0f;

    for(int i = 0; i < Graphics::RENDER_BUFFER_COUNT; ++i)
    {
		float invMapSize = 1.0f / static_cast<float>(RainService::DEPTH_MAP_SIZE);
		m_cpuUpdateBuffer[i].gRainInvMapSize.x = invMapSize;
		m_cpuUpdateBuffer[i].gRainInvMapSize.y = invMapSize;
		m_cpuWetnessBuffer[i].gInvScreen = Math::Vec2f(1.0f / App::WINDOW_WIDTH, 1.0f / App::WINDOW_HEIGHT);

        m_cpuWetnessBuffer[i].gProjAB.x = (1.0f / f) - (1.0f / n); // A
        m_cpuWetnessBuffer[i].gProjAB.y = (1.0f / n);              // B
    }

    CreateWetnessResources(pDevice, WETNESS_MAP_SIZE, WETNESS_MAP_SIZE);

#ifdef _DEBUG
	BIND_DEBUG_SLIDER_INT("Rain", "spawnPerFrame", (int*)&m_spawnPerFrame, 0, RainParticlePool::MaxSpawnPerFrame);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "windPower", &m_windPower, 0.0f, 10.0f, 0.01f);

	BIND_DEBUG_SLIDER_FLOAT("Rain", "spawnRadius", &gDebugSpawnRadius, 0.0f, 100.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "gravity", &gDebugGravity, 0.0f, 20.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "heightOffset", &gDebugHeightOffset, 0.0f, 200.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "addSize", &gDebugRainAddSize, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "life", &gDebugLife, 0.1f, 20.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "baseLength", &gDebugRainBaseLength, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "baseWidth", &gDebugRainBaseWidth, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speedToLength", &gDebugRainSpeedToLength, 0.0f, 0.1f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "lifeFade", &gDebugLifeFade, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetStrength", &gDebugWetStrength, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetMinNdotUp", &gDebugWetMinNdotUp, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetEdgeThreshold", &gDebugWetEdgeThreshold, 0.0f, 10.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetEdgeSharpness", &gDebugWetEdgeSharpness, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetNearThickZ", &gDebugWetNearThickZ, 0.1f, 100.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetFarThickZ", &gDebugWetFarThickZ, 1.0f, 100.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetUpMin", &gDebugWetUpMin, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetUpMax", &gDebugWetUpMax, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "wetFarFade", &gDebugWetFarFade, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speckleCellSize", &gDebugSpeckleCellSize, 0.0001f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speckleDensity", &gDebugSpeckleDensity, 0.1f, 10.0f, 0.1f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speckleAmount", &gDebugSpeckleAmount, 0.01f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("Rain", "speckleTimeHz", &gDebugSpeckleTimeHz, 0.1f, 60.0f, 0.1f);
#endif
}

void RainService::Commit(double deltaTime)
{
    Graphics::DX11::BufferUpdateDesc updateDesc{};

    auto& spawnBuf = m_cpuSpawnBuffer[currentSlot];
    auto& updateBuf = m_cpuUpdateBuffer[currentSlot];
	auto& matrixBuf = m_cpuMatrixBuffer[currentSlot];
    auto& renderBuf = m_cpuRenderBuffer[currentSlot];
	auto& wetnessBuf = m_cpuWetnessBuffer[currentSlot];
    auto& wetnessScrollBuf = m_cpuWetScrollBuffer[currentSlot];
    auto& wetnessUpdateBuf = m_cpuWetUpdateBuffer[currentSlot];

    {
        std::lock_guard lock(bufMutex);

        spawnBuf.gTime = m_elapsedTime;
        spawnBuf.gSpawnRadius = m_spawnRadius;


        updateBuf.gDt = static_cast<float>(deltaTime);
        updateBuf.gTime = m_elapsedTime;
		updateBuf.gSpawnRadius = m_spawnRadius;

		wetnessBuf.gTime = m_elapsedTime;
		wetnessBuf.gWetInvWorldSize = 1.0f / mWetWorldSize;

        const auto& cameraPosWS = spawnBuf.gCamPos;

        const float metersPerTexelX = mWetWorldSize / float(mWetW);
        const float metersPerTexelY = mWetWorldSize / float(mWetH);

        Math::Vec2f originFine{
           cameraPosWS.x - mWetWorldSize * 0.5f,
           cameraPosWS.z - mWetWorldSize * 0.5f
        };

        // スクロール用のスナップorigin（更新の安定化）
        Math::Vec2f originSnap;
        originSnap.x = std::floor(originFine.x / metersPerTexelX) * metersPerTexelX;
        originSnap.y = std::floor(originFine.y / metersPerTexelY) * metersPerTexelY;

        // “欲しいsnap”をテクセル整数で決める
        int32_t newTx = (int32_t)std::floor(originFine.x / metersPerTexelX);
        int32_t newTy = (int32_t)std::floor(originFine.y / metersPerTexelY);

        // スクロール量も整数差分で確定（丸め誤差なし）
        int32_t dxTex = newTx - mWetOriginTexelX;
        int32_t dyTex = newTy - mWetOriginTexelY;

        // snap原点を更新
        mWetOriginTexelX = newTx;
        mWetOriginTexelY = newTy;
        mWetOriginXZ.x = float(mWetOriginTexelX) * metersPerTexelX;
        mWetOriginXZ.y = float(mWetOriginTexelY) * metersPerTexelY;

        // 表示用の sub-texel オフセット（0..1texel 未満）
        Math::Vec2f originSub{
            originFine.x - originSnap.x,
            originFine.y - originSnap.y
        };

        // CBへ
        wetnessBuf.gWetOriginXZ_Snap = mWetOriginXZ; // Snap

        float metersPerTexel = mWetWorldSize / DEPTH_MAP_SIZE;
        float desiredBlurMeters = 10.0f;
        float blurRadiusTexels = desiredBlurMeters / metersPerTexel;

        wetnessBuf.gBlurRadiusTexels = blurRadiusTexels;

        wetnessScrollBuf.scrollTexel[0] = dxTex;
        wetnessScrollBuf.scrollTexel[1] = dyTex;
        wetnessScrollBuf.initWetness = m_initWetnessForNewArea;
        wetnessScrollBuf.texSize[0] = mWetW;
        wetnessScrollBuf.texSize[1] = mWetH;

        wetnessUpdateBuf.dt = static_cast<float>(deltaTime);
        wetnessUpdateBuf.dryRate = m_dryRate;
        wetnessUpdateBuf.rainRate = m_rainRate;
        wetnessUpdateBuf.globalWet = m_globalWet;
        wetnessUpdateBuf.texSize[0] = mWetW;
        wetnessUpdateBuf.texSize[1] = mWetH;

		wetnessUpdateBuf.gWetOriginXZ = mWetOriginXZ;
		wetnessUpdateBuf.gWetWorldSize = mWetWorldSize;
		wetnessUpdateBuf.gTimeSec = m_elapsedTime;


#ifdef _DEBUG
        m_spawnRadius = gDebugSpawnRadius;

		spawnBuf.gHeightOffset = gDebugHeightOffset;
		spawnBuf.gAddSize = gDebugRainAddSize;
		spawnBuf.gLife = gDebugLife;
        updateBuf.gGravity = gDebugGravity;
		renderBuf.gBaseLength = gDebugRainBaseLength;
		renderBuf.gBaseWidth = gDebugRainBaseWidth;
		renderBuf.gSpeedToLength = gDebugRainSpeedToLength;
		renderBuf.gLifeFade = gDebugLifeFade;
		wetnessBuf.gWetStrength = gDebugWetStrength;
		wetnessBuf.gWetMinNdotUp = gDebugWetMinNdotUp;
		wetnessBuf.gEdgeThreshold = gDebugWetEdgeThreshold;
		wetnessBuf.gEdgeSharpness = gDebugWetEdgeSharpness;
		wetnessBuf.gNearThickZ = gDebugWetNearThickZ;
		wetnessBuf.gFarThickZ = gDebugWetFarThickZ;
		wetnessBuf.gUpMin = gDebugWetUpMin;
		wetnessBuf.gUpMax = gDebugWetUpMax;
		wetnessBuf.gFarFade = gDebugWetFarFade;
		wetnessUpdateBuf.gSpeckleCellSize = gDebugSpeckleCellSize;
		wetnessUpdateBuf.gSpeckleDensity = gDebugSpeckleDensity;
		wetnessUpdateBuf.gSpeckleAmount = gDebugSpeckleAmount;
		wetnessUpdateBuf.gSpeckleTimeHz = gDebugSpeckleTimeHz;
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

	updateDesc.buffer = m_matrixCB.Get();
	updateDesc.size = sizeof(MatrixCB);
	updateDesc.data = &matrixBuf;
	m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_renderCB.Get();
    updateDesc.size = sizeof(RenderCB);
    updateDesc.data = &renderBuf;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

	updateDesc.buffer = m_wetnessCB.Get();
	updateDesc.size = sizeof(WetnessCB);
	updateDesc.data = &wetnessBuf;
	m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_wetScrollCB.Get();
    updateDesc.size = sizeof(WetnessScrollCB);
    updateDesc.data = &wetnessScrollBuf;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_wetUpdateCB.Get();
    updateDesc.size = sizeof(WetnessUpdateCB);
    updateDesc.data = &wetnessUpdateBuf;
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
		m_matrixCB.Get(),
		m_rainVS.Get(),
		m_rainPS.Get(),
		m_renderCB.Get(),
        lightData,
        m_spawnPerFrame
        );
}


void RainService::UpdateWetnessCS(ID3D11DeviceContext* ctx)
{
    // --- 1) ScrollCopyCS (Prev -> New) ---
    {
        ID3D11ShaderResourceView* srvs[] = { m_WetPrevSRV.Get() };
        ID3D11UnorderedAccessView* uavs[] = { m_WetNewUAV.Get() };
        UINT initialCounts[] = { 0 };

        ctx->CSSetShader(m_wetScrollCopyCS.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, m_wetScrollCB.GetAddressOf());
        ctx->CSSetShaderResources(0, 1, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

        const UINT gx = (mWetW + 7) / 8;
        const UINT gy = (mWetH + 7) / 8;
        ctx->Dispatch(gx, gy, 1);

        // unbind
        ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
        ID3D11ShaderResourceView* nullSRV[] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetShader(nullptr, nullptr, 0);
    }

    // --- 2) UpdateWetnessCS (in-place on New) ---
    {
        ID3D11UnorderedAccessView* uavs[] = { m_WetNewUAV.Get() };
        UINT initialCounts[] = { 0 };

        ctx->CSSetShader(m_wetUpdateCS.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, m_wetUpdateCB.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

        const UINT gx = (mWetW + 7) / 8;
        const UINT gy = (mWetH + 7) / 8;
        ctx->Dispatch(gx, gy, 1);

        ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
    }

    // --- 3) swap Prev/New ---
    std::swap(m_WetPrevTex, m_WetNewTex);
    std::swap(m_WetPrevSRV, m_WetNewSRV);
    std::swap(m_WetPrevUAV, m_WetNewUAV);
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

bool RainService::CreateWetnessResources(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    mWetW = w; mWetH = h;

    DXGI_FORMAT fmt = DXGI_FORMAT_R16_FLOAT;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(dev->CreateTexture2D(&td, nullptr, &m_WetPrevTex))) return false;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &m_WetNewTex)))  return false;

    // SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    if (FAILED(dev->CreateShaderResourceView(m_WetPrevTex.Get(), &sd, &m_WetPrevSRV))) return false;
    if (FAILED(dev->CreateShaderResourceView(m_WetNewTex.Get(), &sd, &m_WetNewSRV)))  return false;

    // UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = fmt;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    ud.Texture2D.MipSlice = 0;

    if (FAILED(dev->CreateUnorderedAccessView(m_WetPrevTex.Get(), &ud, &m_WetPrevUAV))) return false;
    if (FAILED(dev->CreateUnorderedAccessView(m_WetNewTex.Get(), &ud, &m_WetNewUAV)))  return false;

    return true;
}

