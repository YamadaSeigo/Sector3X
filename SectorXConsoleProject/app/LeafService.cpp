#include "LeafService.h"
#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>

#include <SectorFW/Debug/UIBus.h>

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

HRESULT CreateLeafGuideCurveBuffer(
    ID3D11Device* dev,
    ID3D11Buffer** outBuf,
    ID3D11ShaderResourceView** outSRV
)
{
    if (!dev || !outBuf || !outSRV) return E_INVALIDARG;

    *outBuf = nullptr;
    *outSRV = nullptr;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(LeafService::GuideCurve) * LeafService::TotalGuideCurves;
    desc.Usage = D3D11_USAGE_DYNAMIC;                    // 毎フレーム更新ならOK
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(LeafService::GuideCurve);

    HRESULT hr = dev->CreateBuffer(&desc, nullptr, outBuf);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = LeafService::TotalGuideCurves;

    hr = dev->CreateShaderResourceView(*outBuf, &srv, outSRV);
    if (FAILED(hr))
    {
        (*outBuf)->Release();
        *outBuf = nullptr;
        return hr;
    }

    return S_OK;
}

HRESULT CreateLeafClumpBuffer(
    ID3D11Device* dev,
    ID3D11Buffer** outBuf,
    ID3D11ShaderResourceView** outSRV,
    ID3D11UnorderedAccessView** outUAV,
	D3D11_SUBRESOURCE_DATA* initialData = nullptr
)
{
    if (!dev || !outBuf || !outSRV || !outUAV) return E_INVALIDARG;

    *outBuf = nullptr;
    *outSRV = nullptr;
    *outUAV = nullptr;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(LeafService::Clump) * LeafService::TotalClumps;
    desc.Usage = D3D11_USAGE_DEFAULT; // GPU書き込み用
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(LeafService::Clump);


    HRESULT hr = dev->CreateBuffer(&desc, initialData, outBuf);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = LeafService::TotalClumps;

    hr = dev->CreateShaderResourceView(*outBuf, &srv, outSRV);
    if (FAILED(hr)) { (*outBuf)->Release(); *outBuf = nullptr; return hr; }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.FirstElement = 0;
    uav.Buffer.NumElements = LeafService::TotalClumps;
    uav.Buffer.Flags = 0; // Append/Counter使うならここを設定

    hr = dev->CreateUnorderedAccessView(*outBuf, &uav, outUAV);
    if (FAILED(hr))
    {
        (*outSRV)->Release(); *outSRV = nullptr;
        (*outBuf)->Release(); *outBuf = nullptr;
        return hr;
    }

    return S_OK;
}


#ifdef _DEBUG
float gDebugLeafAddSize = 0.03f;
float gDebugLeafBaseSize = 0.1f;
float gDebugLeafLaneMax = 1.2f;
float gDebugLeafRadialMax = 0.25f;
#endif

LeafService::LeafService(
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    Graphics::DX11::BufferManager* bufferMgr,
    const wchar_t* csInitFreeListPath,
    const wchar_t* csClumpUpdatePath,
    const wchar_t* csSpawnPath,
    const wchar_t* csUpdatePath,
    const wchar_t* csArgsPath,
    const wchar_t* vsPath,
    const wchar_t* psPath)
    : m_bufferMgr(bufferMgr)
{

    // ガイド曲線パラメータ初期化
    InitCurveParams(12345);

    InitClumpsCPU(67890, 6.0f, 5.0f);

    // ボリュームバッファ作成
    ID3D11Buffer* buf = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;

    CreateLeafVolumeBuffer(
        pDevice,
        &buf,
        &srv);
    m_volumeBuffer.Attach(buf);
    m_volumeSRV.Attach(srv);

    CreateLeafGuideCurveBuffer(
        pDevice,
        &buf,
        &srv
    );
	m_guideCurveBuffer.Attach(buf);
    m_guideCurveSRV.Attach(srv);

	D3D11_SUBRESOURCE_DATA initialData{};
	initialData.pSysMem = m_cpuClumps;

	//初期化時にCPU側で作成したクラスターデータを転送
	//そのあとはGPU側で更新していく
    CreateLeafClumpBuffer(
        pDevice,
        &buf,
        &srv,
        &uav,
        &initialData
    );
    m_clumpBuffer.Attach(buf);
    m_clumpSRV.Attach(srv);
	m_clumpUAV.Attach(uav);

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(ClumpUpdateCB);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    pDevice->CreateBuffer(&desc, nullptr, m_clumpUpdateCB.GetAddressOf());

	desc.ByteWidth = sizeof(SpawnCB);
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
	compileShader(csClumpUpdatePath, m_clumpUpdateCS);
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

#ifdef _DEBUG
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "addSize", &gDebugLeafAddSize, 0.0f, 1.0f, 0.001f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "baseSize", &gDebugLeafBaseSize, 0.01f, 1.0f, 0.001f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "laneMax", &gDebugLeafLaneMax, 0.01f, 10.0f, 0.01f);
    BIND_DEBUG_SLIDER_FLOAT("Leaf", "radialMax", &gDebugLeafRadialMax, 0.01f, 10.0f, 0.01f);
#endif

    BIND_DEBUG_CHECKBOX("Leaf", "chasePlayer", &isChasePlayer);
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

	float dt = static_cast<float>(deltaTime);

    // GPUも更新
    updateDesc.buffer = m_guideCurveBuffer;
    updateDesc.size = sizeof(GuideCurve) * TotalGuideCurves;
    updateDesc.data = m_cpuGuideCurves;
    updateDesc.isDelete = false;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

	//更新はGPUで行うためコメントアウト(UAVを使用しているのでMapできない)
    //UpdateClumpsCPU(dt, activeSize, 0.2f, 0.1f);

	/*updateDesc.buffer = m_clumpBuffer;
	updateDesc.size = sizeof(Clump) * activeSize * ClumpsPerVolume;
	updateDesc.data = m_cpuClumps;
	updateDesc.isDelete = false;
	m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);*/

    auto& clumpUpdateBuf = m_cpuClumpUpdateBuffer[currentSlot];
    auto& spawnBuf = m_cpuSpawnBuffer[currentSlot];
    auto& updateBuf = m_cpuUpdateBuffer[currentSlot];
    auto& camBuf = m_cpuCameraBuffer[currentSlot];

    {
        std::lock_guard lock(bufMutex);

		clumpUpdateBuf.gDt = dt;
		clumpUpdateBuf.gTime = m_elapsedTime;
		clumpUpdateBuf.gActiveVolumeCount = activeSize;

        spawnBuf.gActiveVolumeCount = activeSize;
        spawnBuf.gTime = m_elapsedTime;

        updateBuf.gDt = dt;
        updateBuf.gTime = m_elapsedTime;

        camBuf.gTime = m_elapsedTime;

#ifdef _DEBUG
        spawnBuf.gAddSizeScale = gDebugLeafAddSize;
        camBuf.gSize = gDebugLeafBaseSize;

        spawnBuf.gLaneMax = gDebugLeafLaneMax;
        spawnBuf.gRadialMax = gDebugLeafRadialMax;
#endif
    }

    updateDesc.buffer = m_clumpUpdateCB.Get();
    updateDesc.size = sizeof(ClumpUpdateCB);
    updateDesc.data = &clumpUpdateBuf;
    updateDesc.isDelete = false;
    updateDesc.isArray = false;
    m_bufferMgr->UpdateBuffer(updateDesc, currentSlot);

    updateDesc.buffer = m_spawnCB.Get();
    updateDesc.size = sizeof(SpawnCB);
    updateDesc.data = &spawnBuf;
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

void LeafService::SpawnParticles(
    ID3D11DeviceContext* ctx,
    ComPtr<ID3D11ShaderResourceView>& heightMap,
    ComPtr<ID3D11ShaderResourceView>& leafTex,
    ComPtr<ID3D11ShaderResourceView>& depthSRV,
    ComPtr<ID3D11Buffer>& terrainCB,
    ComPtr<ID3D11Buffer>& windCB,
    uint32_t slot)
{
    m_particlePool.Spawn(
        ctx,
        m_clumpUpdateCS.Get(),
        m_spawnCS.Get(),
        m_updateCS.Get(),
        m_argsCS.Get(),
        m_volumeSRV.Get(),
		m_guideCurveSRV.Get(),
		m_clumpSRV.Get(),
        heightMap.Get(),
        leafTex.Get(),
        depthSRV.Get(),
		m_clumpUAV.Get(),
		m_clumpUpdateCB.Get(),
        m_spawnCB.Get(),
        terrainCB.Get(),
        windCB.Get(),
        m_updateCB.Get(),
        m_cameraCB.Get(),
        m_leafVS.Get(),
        m_leafPS.Get(),
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

        // 左右交互は維持（束感が出やすい）
        float side = (i & 1) ? 1.0f : -1.0f;

        // 曲率は「長さに比例」させる（棒を回避）
        // 例：L=10m -> bendBase ~ 2m、L=20m -> bendBase ~ 4m くらい
        float bendBase = p.length * RandRange(s, 0.25f, 0.4f);   // 0.15..0.25 * L
        float bendJit = RandRange(s, -0.4f, 0.4f);              // 微妙に崩す
        p.bend = (bendBase + bendJit) * side;

        // start/end オフセットは「横(right)」を強め・「前後(fwd)」は弱め
        // ここでのXZはローカル(X=right, Z=fwd扱い)として使う想定
        // （あなたのBuildで Vec3{start.x, 0, start.y} としているので、start.yがZになります）
        float startRight = RandRange(s, -0.6f, 0.6f);            // startは小さく
        float startFwd = RandRange(s, -0.4f, 0.4f);

        float endRight = RandRange(s, -1.5f, 1.5f);            // endは少し広め
        float endFwd = RandRange(s, -1.0f, 1.0f);

        p.startOffXZ = { startRight, startFwd };
        p.endOffXZ = { endRight,   endFwd };

        // optional: コントロール点の前進割合を個体差（対称を崩す）
        p.t1 = RandRange(s, 0.25f, 0.40f); // P1のz比率
        p.t2 = RandRange(s, 0.55f, 0.80f); // P2のz比率

        // optional: P1/P2の横振り非対称係数（S字を自然に）
        p.bendAsym = RandRange(s, 0.65f, 1.20f);

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

        // ゆっくり揺らす（曲率が目に見えるように、少しだけ動かす）
        float wob = 0.85f + 0.15f * std::sinf(timeSec * 0.7f + i * 0.31f);
        float bend = prm.bend * wob;

        // ローカル: X=right, Y=up, Z=fwd
        Vec3f P0 = { prm.startOffXZ.x, 0.0f, prm.startOffXZ.y };
        Vec3f P3 = { prm.endOffXZ.x,   0.0f, prm.endOffXZ.y + L };

        // 進行方向の割合（CurveParamsに持たせた場合）
        float t1 = prm.t1; // 0.25..0.40
        float t2 = prm.t2; // 0.55..0.80

        // 横振りの非対称（S字を自然に）
        float b1 = bend * prm.bendAsym;
        float b2 = -bend;

        // P1/P2 の横振りは「m単位」になってOK（Lに比例してるので自然）
        // ただし、P0のrightとP3のrightとの差も少し混ぜると束が崩れにくい
        float right0 = P0.x;
        float right3 = P3.x;
        float rightLerp1 = std::lerp(right0, right3, t1);
        float rightLerp2 = std::lerp(right0, right3, t2);

        Vec3f P1 = { rightLerp1 + b1, 0.0f, P0.z + L * t1 };
        Vec3f P2 = { rightLerp2 + b2, 0.0f, P0.z + L * t2 };

        GuideCurve& c = m_cpuGuideCurves[i];
        c.p0L = P0;
        c.p1L = P1;
        c.p2L = P2;
        c.p3L = P3;

        // 長さは厳密じゃなくてOKだけど、S字は少し長くなるので軽く補正してもいい
        // 例：|bend| が大きいほど長い
        c.lengthApprox = L * (1.0f + 0.08f * (std::min)(std::abs(bend) / (std::max)(L, 1e-3f), 1.0f));
    }
}

void LeafService::InitClumpsCPU(uint32_t baseSeed, float laneMax, float radialMax)
{

    for (uint32_t volIdx = 0; volIdx < MaxVolumes; ++volIdx)
    {
        const uint32_t curveBase = volIdx * CurvePerVolume;

        for (uint32_t c = 0; c < ClumpsPerVolume; ++c)
        {
            uint32_t s = Hash(baseSeed ^ (volIdx * 9781u) ^ (c * 6271u));

            Clump cl{};
            cl.seed = s;

            // どのカーブに乗るか（volume内から）
            cl.curveId = curveBase + (Hash(s) % CurvePerVolume);

            // 進行度はバラす（最初から塊が散っている）
            cl.s = RandRange(s, 0.0f, 1.0f);

            // 塊の中心オフセット（塊ごとにレーンが違う）
            // “群れ”にしたいなら中心は広めに散らし、粒子個体差は小さめにするのがコツ
            cl.laneCenter = RandRange(s, -laneMax, laneMax);
            cl.radialCenter = RandRange(s, -radialMax, radialMax);

            // clumpごとの速度ブレ（少しだけ）
            cl.speedMul = RandRange(s, 0.85f, 1.15f);

            // 揺れ位相
            cl.phase = RandRange(s, 0.0f, 6.2831853f);

			// Yオフセット（地面からの高さ調整用）
			cl.yOffset = RandRange(s, -0.5f, 0.5f);

            cl.yVel = 0.0f;

            m_cpuClumps[size_t(volIdx) * ClumpsPerVolume + c] = cl;
        }
    }
}

void LeafService::UpdateClumpsCPU(float dt, uint32_t activeVolumeCount, float laneAmp, float radialAmp)
{
    for (uint32_t volIdx = 0; volIdx < activeVolumeCount; ++volIdx)
    {
		const auto& volume = m_activeVolumes[volIdx];

        for (uint32_t i = 0; i < ClumpsPerVolume; ++i)
        {
            Clump& cl = m_cpuClumps[volIdx * ClumpsPerVolume + i];

            float sp = volume.speed * cl.speedMul;
            cl.s = std::fmod(cl.s + (sp * dt / (std::max)(m_cpuGuideCurves[cl.curveId].lengthApprox, 0.001f)), 1.0f);
            if (cl.s < 0.0f) cl.s += 1.0f;

            // “群れのうねり”（塊が左右上下に同調して揺れる）
            cl.laneCenter += std::sin(m_elapsedTime * 0.7f + cl.phase) * laneAmp * dt;
            cl.radialCenter += std::sin(m_elapsedTime * 0.9f + cl.phase * 1.3f) * radialAmp * dt;

            // 逸脱しすぎ防止（クランプ）
			float laneCenterLimit = volume.radius;
			float radialLimit = volume.radius * 0.5f;
            cl.laneCenter = std::clamp(cl.laneCenter, -laneCenterLimit, laneCenterLimit);
            cl.radialCenter = std::clamp(cl.radialCenter, -radialLimit, radialLimit);
        }
    }
}
