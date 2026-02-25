#pragma once

#include "RainParticlePool.h"

class RainService : public ECS::IUpdateService, public ECS::ICommitService
{
public:

	constexpr inline static uint32_t DEPTH_MAP_SIZE = 512;

    struct SpawnCB
    {
        Math::Vec3f gCamPos = {};
        float gTime = 0.0f;

        uint32_t gMaxSpawnPerFrame = RainParticlePool::MaxSpawnPerFrame; // 例：32
        uint32_t gMaxParticles = RainParticlePool::MaxParticles; // FreeList枯渇対策（使わなくてもOK）
        float gHeightOffset = 50.0f; // カメラからの高さオフセット
        float gAddSize = 0.02f;

        float gSpawnRadius = 80.0f; // スポーン位置の半径
		float gLife = 5.0f; // 例: 5秒
        float _pad0, _pad1; // 16B 境界揃え
    };

    struct UpdateCB
    {
        float gDt = 0.0f;
        float gTime = 0.0f;
        float gGravity = 9.8f;

        float pad0 = {};

        Math::Vec3f gCamPosWS = {};
        float gSpawnRadius = 80.0f;

        Math::Vec3f gWindWS = {};
        float pad1 = {};

        Math::Matrix4x4f gCamViewProj = {};
        Math::Vec2f gRainInvMapSize = {}; // (1/width, 1/height)
        float gRainDepthBias = 1e-3f;
        float padding2 = {};
    };

    struct RenderCB
    {
        Math::Matrix4x4f gViewProj = {};
        Math::Vec3f gCamPosWS = {};
        float _pad0 = {};

        float gBaseWidth = 0.025f; // 例: 0.01～0.03 (m)
        float gBaseLength = 0.01f; // 例: 0.10～0.40 (m)
        float gSpeedToLength = 0.02f; // 例: 0.01～0.03 (length += |v| * k)
        float gMinSpeedForDir = 0.1f; // 例: 0.1

        float gAlpha = 0.5f; // 全体alpha
        float gLifeFade = 0.18f; // 例: 1.0 (寿命で薄くする強さ)
        float _pad3[2] = {};
    };

    struct WetnessCB
    {
        Math::Vec2f gWetOriginXZ = {}; // このWetnessRTがカバーするワールド原点(XZ)
        float gWetInvWorldSize = 0.0f; // 1 / カバーするワールド幅（例: 1/256m）
        float gWetStrength = 1.0f; // 全体強度

        float gWetDarken = 0.35f; // 濡れで暗くする量(例: 0.35)
        float gWetSpecBoost = 1.0f; // 疑似スペキュラ強度(例: 1.0)
        float gWetFlatten = 0.6f; // 法線コントラスト抑制(例: 0.6)
        float gWetMinNdotUp = 0.2f; // 上面のみ濡らす閾値(例: 0.2)
    };

    static constexpr uint32_t MaxVolumes = 32;

    RainService(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
        Graphics::DX11::BufferManager* bufferMgr,
        const wchar_t* csInitFreeListPath,
        const wchar_t* csSpawnPath,
        const wchar_t* csUpdatePath,
        const wchar_t* csArgsPath,
        const wchar_t* vsPath,
        const wchar_t* psPath);

    void PreUpdate(double deltaTime) override {
        currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;
        m_elapsedTime += static_cast<float>(deltaTime);
    }

    void SetCameraPos(const Math::Vec3f pos) {
        std::lock_guard lock(bufMutex);
        m_cpuSpawnBuffer[currentSlot].gCamPos = pos;
        m_cpuUpdateBuffer[currentSlot].gCamPosWS = pos;
		m_cpuRenderBuffer[currentSlot].gCamPosWS = pos;

		auto depthMapViewProj = MakeDepthMapViewProjNoLock();
		m_cpuUpdateBuffer[currentSlot].gCamViewProj = depthMapViewProj;
    }

    void SetCameraBuffer(const RenderCB& camCB) {
        std::lock_guard lock(bufMutex);
        m_cpuRenderBuffer[currentSlot] = camCB;
    }

    void SetWind(const Math::Vec3f wind) {
        std::lock_guard lock(bufMutex);
        m_cpuUpdateBuffer[currentSlot].gWindWS = wind;
	}

    void Commit(double deltaTime) override;

    void ClearDepthMap(ID3D11DeviceContext* ctx);

    void SpawnDrawParticles(ID3D11DeviceContext* ctx, RainParticlePool::TiledLightData* lightData);

    float GetElapsedTime() const noexcept {
        return m_elapsedTime;
    }

    void SetSpawnPerFrame(uint32_t count) {
        std::lock_guard lock(bufMutex);
        m_spawnPerFrame = (std::min)(count, RainParticlePool::MaxSpawnPerFrame);
	}

	Graphics::BufferHandle GetSpawnCBHandle() const { return m_spawnCBHandle; }
	Graphics::BufferHandle GetUpdateCBHandle() const { return m_updateCBHandle; }
	Graphics::BufferHandle GetRenderCBHandle() const { return m_renderCBHandle; }
	Graphics::BufferHandle GetSplashCBHandle() const { return m_wetnessCBHandle; }

	ComPtr<ID3D11Buffer> GetSpawnCB() const { return m_spawnCB; }
	ComPtr<ID3D11Buffer> GetUpdateCB() const { return m_updateCB; }
	ComPtr<ID3D11Buffer> GetRenderCB() const { return m_renderCB; }
    ComPtr<ID3D11Buffer> GetWetnessCB() const { return m_wetnessCB; }

	ComPtr<ID3D11DepthStencilView> GetDepthMapDSV() const { return m_depthMapDSV; }
	ComPtr<ID3D11ShaderResourceView> GetDepthMapSRV() const { return m_depthMapSRV; }

private:
	Math::Matrix4x4f MakeDepthMapViewProjNoLock() const;

private:
    // ---- GPUリソース ----
    ComPtr<ID3D11Buffer> m_spawnCB;
    ComPtr<ID3D11Buffer> m_updateCB;
    ComPtr<ID3D11Buffer> m_renderCB;
	ComPtr<ID3D11Buffer> m_wetnessCB;

    ComPtr<ID3D11ComputeShader> m_initFreeListCS;
    ComPtr<ID3D11ComputeShader> m_spawnCS;
    ComPtr<ID3D11ComputeShader> m_updateCS;
    ComPtr<ID3D11ComputeShader> m_argsCS;

    ComPtr<ID3D11VertexShader> m_rainVS;
    ComPtr<ID3D11PixelShader> m_rainPS;

	ComPtr<ID3D11Texture2D> m_depthMap;
	ComPtr<ID3D11DepthStencilView> m_depthMapDSV;
	ComPtr<ID3D11ShaderResourceView> m_depthMapSRV;

    Graphics::DX11::BufferManager* m_bufferMgr = nullptr;

    RainParticlePool m_particlePool;

	Graphics::BufferHandle m_spawnCBHandle;
	Graphics::BufferHandle m_updateCBHandle;
	Graphics::BufferHandle m_renderCBHandle;
	Graphics::BufferHandle m_wetnessCBHandle;

    std::mutex bufMutex;
    SpawnCB m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    RenderCB m_cpuRenderBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	WetnessCB m_cpuWetnessBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

    uint32_t currentSlot = 0;
    float m_elapsedTime = 0.0f;

    uint32_t m_spawnPerFrame = 32 << 2;

public:
    STATIC_SERVICE_TAG
};
