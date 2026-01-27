// LeafService.h

#pragma once

#include "LeafParticlePool.h"   // FreeList/Alive バッファの仕組みを利用
#include <vector>
#include <unordered_map>
#include <mutex>


struct LeafVolumeGPU
{
    Math::Vec3f centerWS = {};
    float       radius = 1.0f;

    Math::Vec3f color = { 1.0f, 1.0f, 1.0f };
    float       intensity = 1.0f;

    float       targetCount = 100.0f;     // LOD後の最終個数
    float       speed = 20.0f;      // 風での移動速度
    float       noiseScale = 0.5f;
    uint32_t    volumeSlot = 0;         // GPU側 slot index

    uint32_t    seed = 0;
	uint32_t pad0 = 0;

    float    pad1[2] = {};
};

static_assert(sizeof(LeafVolumeGPU) % 16 == 0, "LeafVolumeGPU must be 16-byte aligned");


class LeafService : public ECS::IUpdateService, public ECS::ICommitService
{
public:

    static constexpr uint32_t MaxVolumes = 16;
    static constexpr uint32_t CurvePerVolume = 16;
    static constexpr uint32_t TotalGuideCurves = CurvePerVolume * MaxVolumes;
    static constexpr uint32_t ClumpsPerVolume = 8;
    static constexpr uint32_t TotalClumps = ClumpsPerVolume * MaxVolumes;

    struct ClumpUpdateCB
    {
        float gDt = 0.0f;
        float gTime = 0.0f;
        uint32_t gActiveVolumeCount = 0;
        uint32_t gClumpsPerVolume = ClumpsPerVolume;

        uint32_t gCurvesPerVolume = CurvePerVolume; // 0なら全体共有とみなす
        float gClumpLength01 = 12.0f; // 粒子側で使う: cl.s からの前後散らし幅（ここでは使用しない）

        // ---- Swarm / wobble ----
        float gClumpLaneAmp = 1.0f; // 例: 0.2～1.0（radiusに対して）
        float gClumpRadialAmp = 0.3f; // 例: 0.05～0.3
        float gClumpLaneFreq = 0.7f; // 例: 0.7
        float gClumpRadialFreq = 0.9f; // 例: 0.9

        // ---- Ground follow ----
        float gGroundBase = 0.25f; // 例: 0.25 (m above ground)
        float gGroundWaveAmp = 0.35f; // 例: 0.35
        float gGroundWaveFreq = 0.8f; // 例: 0.8
        float gGroundFollowK = 6.0f; // 例: 6.0 (spring strength)
        float gGroundFollowD = 1.2f; // 例: 1.2 (damping on y-velocity)

        // ---- Limits ----
        float gLaneLimitScale = 1.0f; // 例: 1.0 (lane limit = radius*scale)
        float gRadialLimitScale = 0.5f; // 例: 0.5
        float gMaxYOffset = 5.0f; // 例: 5.0 (safety clamp)

        float pad[2] = {};
    };

    struct SpawnCB
    {
        Math::Vec3f gPlayerPosWS = {};
        float       gTime = 0.0f;

        uint32_t gActiveVolumeCount = 0;
        uint32_t gMaxSpawnPerVolumePerFrame = LeafParticlePool::MaxSpawnPerVol;
		uint32_t gClumpsPerVolume = ClumpsPerVolume; // 1クラスタあたりの群れの塊数
        float    gAddSizeScale = 0.03f; // 葉っぱのサイズばらつき]

        float gLaneMin = 0.6f;
        float gLaneMax = 1.2f;
        float gRadialMin = 0.05f;
        float gRadialMax = 0.25f;
    };


    struct UpdateCB
    {
        float       gDt = 0.0f;
        float       gTime = 0.0f;
        float       pad00[2] = {};

        Math::Vec3f gPlayerPosWS = {};
        float       gPlayerRepelRadius = 2.0f;   // プレイヤーの足元を空けるなら

        uint32_t  gClumpsPerVolume = ClumpsPerVolume;     // 例: 16
        uint32_t  gCurvesPerVolume = CurvePerVolume;     // 例: 32（volumeごとに曲線がまとまってる場合）
        uint32_t  gTotalClumps = TotalClumps;         // activeVolumeCount*gClumpsPerVolume（保険用）
        float gClumpLength01 = 0.12f;       // 例: 0.12（塊の“長さ”= s方向の広がり）
    };

    struct CameraCB
    {
        Math::Matrix4x4f gViewProj = {};
        Math::Vec3f      gCamRightWS = {};
        float            gSize = 0.15f; // 葉っぱの半サイズ
        Math::Vec3f      gCamUpWS = { 0, 1, 0 };
        float            gTime = 0.0f;

        Math::Vec3f gCameraPosWS = {};
        float _padCam0 = {};
        Math::Vec2f gNearFar = {0.1f, 1000.0f}; // (near, far)  ※線形化に使う
        uint32_t gDepthIsLinear01 = 0; // 1: すでに線形(0..1) / 0: D3Dのハードウェア深度
        float _padCam1 = {};
    };

    struct GuideCurve
    {
        Math::Vec3f p0L = {};
        Math::Vec3f p1L = {};
        Math::Vec3f p2L = {};
        Math::Vec3f p3L = {};
        float  lengthApprox = 1.0f;
    };

    struct CurveParams
    {
        float length = 15.0f;
		float bend = 1.0f; //曲がり具合
        Math::Vec2f startOffXZ = {};
        Math::Vec2f endOffXZ = {};

		float t1 = 0.4f, t2 = 0.6f; //p1/p2のz比率
		float bendAsym = 1.0f; // 曲がりの非対称度
    };

    struct Clump
    {
        uint32_t curveId;
        float    s;

        float    laneCenter;
        float    radialCenter;

        float    speedMul;
        float    phase;

        uint32_t seed;
        float yOffset;
        float yVel;

        Math::Vec2f anchorXZ = {};   // clumpの水平アンカー（ボリューム中心からのオフセット）
        Math::Vec2f anchorVelXZ = {};
    };

    LeafService(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        Graphics::DX11::BufferManager* bufferMgr,
        const wchar_t* csInitFreeListPath,
		const wchar_t* csClumpUpdatePath,
        const wchar_t* csSpawnPath,
        const wchar_t* csUpdatePath,
        const wchar_t* csArgsPath,
        const wchar_t* vsPath,
        const wchar_t* psPath);

    // アクティブな Volume を登録（Firefly と同じパターン）
    void PushActiveVolume(uint32_t volumeUID, const LeafVolumeGPU& volume);

    void PreUpdate(double deltaTime) override
    {
        currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

        m_activeVolumes.clear();
        m_elapsedTime += static_cast<float>(deltaTime);
    }

    void SetPlayerPos(const Math::Vec3f pos)
    {
        std::lock_guard lock(bufMutex);
        m_cpuSpawnBuffer[currentSlot].gPlayerPosWS = pos;
        m_cpuUpdateBuffer[currentSlot].gPlayerPosWS = pos;
    }

    void SetCameraBuffer(const CameraCB& camCB)
    {
        std::lock_guard lock(bufMutex);
        m_cpuCameraBuffer[currentSlot] = camCB;
    }

    void Commit(double deltaTime) override;

    // FireflyService::SpawnParticles と同じような形
    void SpawnParticles(
        ID3D11DeviceContext* ctx,
        ComPtr<ID3D11ShaderResourceView>& heightMap,
        ComPtr<ID3D11ShaderResourceView>& leafTex,
        ComPtr<ID3D11ShaderResourceView>& depthSRV,
        ComPtr<ID3D11Buffer>& terrainCB,
        ComPtr<ID3D11Buffer>& windCB,
        uint32_t slot);

    ID3D11ShaderResourceView* GetVolumeSRV() const
    {
        return m_volumeSRV.Get();
    }

    float GetElapsedTime() const noexcept
    {
        return m_elapsedTime;
    }

    bool IsChasePlayer() const noexcept
    {
        return isChasePlayer;
	}

private:
    struct VolumeSlot
    {
        uint32_t volumeUID = 0;
        bool     used = false;
    };

    // CPU 側の管理
    std::vector<LeafVolumeGPU> m_activeVolumes;
    std::unordered_map<uint32_t, uint32_t> m_uidToSlot;
    VolumeSlot  m_slots[MaxVolumes]{};
    uint32_t    m_activeVolumeCount[Graphics::RENDER_BUFFER_COUNT] = { 0 };

    // GPU リソース
    ComPtr<ID3D11Buffer>            m_volumeBuffer;
    ComPtr<ID3D11ShaderResourceView> m_volumeSRV;

	ComPtr<ID3D11Buffer>            m_guideCurveBuffer;
    ComPtr<ID3D11ShaderResourceView> m_guideCurveSRV;

    ComPtr<ID3D11Buffer>            m_clumpBuffer;
    ComPtr<ID3D11ShaderResourceView> m_clumpSRV;
	ComPtr<ID3D11UnorderedAccessView> m_clumpUAV;

	ComPtr<ID3D11Buffer> m_clumpUpdateCB;
    ComPtr<ID3D11Buffer> m_spawnCB;
    ComPtr<ID3D11Buffer> m_updateCB;
    ComPtr<ID3D11Buffer> m_cameraCB;

    ComPtr<ID3D11ComputeShader> m_initFreeListCS;
	ComPtr<ID3D11ComputeShader> m_clumpUpdateCS;
    ComPtr<ID3D11ComputeShader> m_spawnCS;
    ComPtr<ID3D11ComputeShader> m_updateCS;
    ComPtr<ID3D11ComputeShader> m_argsCS;

    ComPtr<ID3D11VertexShader> m_leafVS;
    ComPtr<ID3D11PixelShader>  m_leafPS;

    Graphics::DX11::BufferManager* m_bufferMgr = nullptr;

    LeafParticlePool m_particlePool;

	ClumpUpdateCB m_cpuClumpUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    SpawnCB  m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    CameraCB m_cpuCameraBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

	GuideCurve m_cpuGuideCurves[TotalGuideCurves] = {};
    CurveParams m_curveParams[TotalGuideCurves] = {};

    Clump m_cpuClumps[TotalClumps] = {};

	std::mutex  bufMutex;

    uint32_t currentSlot = 0;
    float    m_elapsedTime = 0.0f;

	bool isChasePlayer = true;

private:
    uint32_t AllocateSlot(uint32_t volumeUID);
    void     ReleaseUnusedSlots();

    void InitCurveParams(uint32_t baseSeed);
	void BuildGuideCurves(float timeSec);

    void InitClumpsCPU(
        uint32_t baseSeed,
        float laneMax,      // 例: 1.5
        float radialMax     // 例: 0.25
    );

    // 毎フレーム更新（最低限：sを進める＋lane/radialをゆらす）
    void UpdateClumpsCPU(
        float dt,
        uint32_t activeVolumeCount,
        float laneAmp,         // 例: 0.6
        float radialAmp        // 例: 0.15
    );

public:
    STATIC_SERVICE_TAG
};
