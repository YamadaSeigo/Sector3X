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
    float       speed = 0.5f;      // 風での移動速度
    float       noiseScale = 0.5f;
    uint32_t    volumeSlot = 0;         // GPU側 slot index

    uint32_t    nearLightBudget = 0;     // 使わないなら 0
    uint32_t    seed = 0;

    float       burstT = 0.0f;  // 0..1（多く舞うタイミングなど）
    float       pad0 = 0.0f;
};

static_assert(sizeof(LeafVolumeGPU) % 16 == 0, "LeafVolumeGPU must be 16-byte aligned");


class LeafService : public ECS::IUpdateService, public ECS::ICommitService
{
public:

    static constexpr uint32_t MaxVolumes = 256;
    static constexpr uint32_t MaxGuideCurves = 32;


    struct SpawnCB
    {
        Math::Vec3f gPlayerPosWS = {};
        float       gTime = 0.0f;

        uint32_t gActiveVolumeCount = 0;
        uint32_t gMaxSpawnPerVolumePerFrame = LeafParticlePool::MaxSpawnPerVol;
		uint32_t gCurvesPerCluster = MaxGuideCurves / 4; // 1クラスタあたりのガイド曲線数
        float    gAddSizeScale = 0.02f; // 葉っぱのサイズばらつき]

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

        float gKillRadiusScale = 1.5f; // e.g. 1.5 (kill if dist > radius*scale)
        float gDamping = 0.96f; // e.g. 0.96
        float gFollowK = 12.0f; // e.g. 12.0  (steer strength)
        float gMaxSpeed = 6.0f; // e.g. 6.0
    };

    struct CameraCB
    {
        Math::Matrix4x4f gViewProj = {};
        Math::Vec3f      gCamRightWS = {};
        float            gSize = 0.15f; // 葉っぱの半サイズ
        Math::Vec3f      gCamUpWS = { 0, 1, 0 };
        float            gTime = 0.0f;
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
    };

    LeafService(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        Graphics::DX11::BufferManager* bufferMgr,
        const wchar_t* csInitFreeListPath,
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

    ComPtr<ID3D11Buffer> m_spawnCB;
    ComPtr<ID3D11Buffer> m_updateCB;
    ComPtr<ID3D11Buffer> m_cameraCB;

    ComPtr<ID3D11ComputeShader> m_initFreeListCS;
    ComPtr<ID3D11ComputeShader> m_spawnCS;
    ComPtr<ID3D11ComputeShader> m_updateCS;
    ComPtr<ID3D11ComputeShader> m_argsCS;

    ComPtr<ID3D11VertexShader> m_leafVS;
    ComPtr<ID3D11PixelShader>  m_leafPS;

    Graphics::DX11::BufferManager* m_bufferMgr = nullptr;

    LeafParticlePool m_particlePool;

    SpawnCB  m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    CameraCB m_cpuCameraBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

	GuideCurve m_cpuGuideCurves[MaxGuideCurves] = {};
    CurveParams m_curveParams[MaxGuideCurves] = {};

	std::mutex  bufMutex;

    uint32_t currentSlot = 0;
    float    m_elapsedTime = 0.0f;

private:
    uint32_t AllocateSlot(uint32_t volumeUID);
    void     ReleaseUnusedSlots();

    void InitCurveParams(uint32_t baseSeed);
	void BuildGuideCurves(float timeSec);

public:
    STATIC_SERVICE_TAG
};
