#pragma once


#include "FireflyParticlePool.h"

struct FireflyVolumeGPU
{
    Math::Vec3f centerWS = {};
    float       radius = 1.0f;

    Math::Vec3f color = {1.0f,1.0f,1.0f};
    float       intensity = 1.0f;

    float       targetCount = 100;     // LOD後の最終個数
    float       speed = 0.1f;
    float       noiseScale = 0.5f;
    uint32_t    volumeSlot = 0;      // GPU側 slot index

    uint32_t    nearLightBudget = 3;
    uint32_t    seed = 0;

    float burstT = 0.0f; // 0..1（1=発動直後、時間で0へ）

	float pad0 = 0.0f;
};


static_assert(sizeof(FireflyVolumeGPU) % 16 == 0);

class FireflyService : public ECS::IUpdateService, public ECS::ICommitService
{
public:
    struct SpawnCB
    {
        Math::Vec3f gPlayerPosWS = {};
        float gTime = 0.0f;

        uint32_t gActiveVolumeCount = 0;
        uint32_t gMaxSpawnPerVolumePerFrame = FireflyParticlePool::MaxSpawnPerVol; // 例：32
        uint32_t gMaxParticles = FireflyParticlePool::MaxParticles; // FreeListにインデックス埋める用
		float gAddSizeScale = 0.02f; // 追加サイズスケール（例: 0.02）
    };

    struct UpdateCB
    {
        float gDt = 0.0f;
        float gTime = 0.0f;
        float pad00[2] = {};
        Math::Vec3f gPlayerPosWS = {};
        float gPlayerRepelRadius = 10.0f;

        Math::Vec3f gCamPosWS = {};
        float gFireflyLightMaxDist = 25.0f;

        uint32_t gPointLightMax = FireflyParticlePool::MaxPointLight;
        float gFireflyLightRange = 3.0f;
        float gFireflyLightIntensity = 1.2f;
        float _pad_up = {};
	};

    struct CameraCB
    {
        Math::Matrix4x4f gViewProj = {};
        Math::Vec3f gCamRightWS = {};
        float gSize = 0.1f; // billboard half-size 例: 0.05
        Math::Vec3f gCamUpWS = {0,1,0};
        float gTime = 0.0f;
    };

    static constexpr uint32_t MaxVolumes = 256;

	FireflyService(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
        Graphics::DX11::BufferManager* bufferMgr,
        const wchar_t* csInitFreeListPath,
        const wchar_t* csSpawnPath,
        const wchar_t* csUpdatePath,
        const wchar_t* csArgsPath,
        const wchar_t* vsPath,
        const wchar_t* psPath);

    void PushActiveVolume(uint32_t volumeUID, const FireflyVolumeGPU& volume);

    void PreUpdate(double deltaTime) override {
		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

        m_activeVolumes.clear();
        m_elapsedTime += static_cast<float>(deltaTime);
    }

    void SetPlayerPos(const Math::Vec3f pos) {
        std::lock_guard lock(bufMutex);
        m_cpuSpawnBuffer[currentSlot].gPlayerPosWS = pos;
		m_cpuUpdateBuffer[currentSlot].gPlayerPosWS = pos;
    }

    void SetCameraBuffer(const CameraCB& camCB) {
        std::lock_guard lock(bufMutex);
        m_cpuCameraBuffer[currentSlot] = camCB;
	}

    void Commit(double deltaTime) override;

	void SpawnParticles(ID3D11DeviceContext* ctx, ComPtr<ID3D11ShaderResourceView>& heightMap, ComPtr<ID3D11Buffer>& terrainCB, uint32_t slot);

    // GPUリソース取得（後段で使用）
    ID3D11ShaderResourceView* GetVolumeSRV() const {
		return m_volumeSRV.Get();
    }

    ID3D11ShaderResourceView* GetPointLightSRV() const {
        return m_pointLight.srv.Get();
    }

    ID3D11Buffer* GetLightCountBuffer() const {
        return m_stagingCountBuf[currentSlot].Get();
    }

    float GetElapsedTime() const noexcept {
        return m_elapsedTime;
	}

private:
    // ---- CPU管理 ----
    struct VolumeSlot
    {
        uint32_t volumeUID;
        bool     used;
    };

    std::vector<FireflyVolumeGPU> m_activeVolumes;
    std::unordered_map<uint32_t, uint32_t> m_uidToSlot;
    VolumeSlot m_slots[MaxVolumes]{};
    uint32_t m_activeVolumeCount[Graphics::RENDER_BUFFER_COUNT] = {0};

    // ---- GPUリソース ----
    ComPtr<ID3D11Buffer> m_volumeBuffer;
    ComPtr<ID3D11ShaderResourceView> m_volumeSRV;

    ComPtr<ID3D11Buffer> m_spawnCB;
	ComPtr<ID3D11Buffer> m_updateCB;
	ComPtr<ID3D11Buffer> m_cameraCB;

    ComPtr<ID3D11Buffer> m_stagingCountBuf[Graphics::RENDER_BUFFER_COUNT];

	ComPtr<ID3D11ComputeShader> m_initFreeListCS;
	ComPtr<ID3D11ComputeShader> m_spawnCS;
    ComPtr<ID3D11ComputeShader> m_updateCS;
    ComPtr<ID3D11ComputeShader> m_argsCS;

	ComPtr<ID3D11VertexShader> m_fireflyVS;
	ComPtr<ID3D11PixelShader> m_fireflyPS;

    StructuredBufferSRVUAV m_pointLight;

	Graphics::DX11::BufferManager* m_bufferMgr = nullptr;

	FireflyParticlePool m_particlePool;

    std::mutex bufMutex;
    SpawnCB m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    CameraCB m_cpuCameraBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

	uint32_t currentSlot = 0;
	float m_elapsedTime = 0.0f;

private:
    uint32_t AllocateSlot(uint32_t volumeUID);
    void     ReleaseUnusedSlots();

public:
    STATIC_SERVICE_TAG
};
