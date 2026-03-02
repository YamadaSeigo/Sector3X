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

        Math::Vec2f gRainInvMapSize = {}; // (1/width, 1/height)
        float gRainDepthBias = 1e-3f;
        float padding2 = {};
    };

    struct MatrixCB
    {
        Math::Matrix4x4f gCamViewProj = {};
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
        float gWetSpecBoost = 0.2f; // 疑似スペキュラ強度(例: 1.0)
        float gWetFlatten = 0.6f; // 法線コントラスト抑制(例: 0.6)
        float gWetMinNdotUp = 0.2f; // 上面のみ濡らす閾値(例: 0.2)

        Math::Vec2f gInvScreen = {}; // 1/width, 1/height
        Math::Vec2f gProjAB = {}; // Linearize用
        float gEdgeThreshold = 2.0f; // エッジ判定しきい値（線形深度の差）
        float gEdgeSharpness = 0.2f; // マスクの鋭さ
        Math::Vec2f gRainDirSS = {0.0f, -1.0f}; // スクリーン空間の雨方向（正規化）
        float gTime = 0.0f;
        float gDensity = 0.8f; // 粒密度
        float gStrength = 0.2f; // 合成強度
        float gDotSizeScale = 1.0f;

        float gNearThickZ = 2.0f;
        float gFarThickZ = 30.0f;
        uint32_t gNearRadiusPx = 3;
        uint32_t gFarRadiusPx = 0;

        float gUpMin = 0.2f;
        float gUpMax = 0.8f;
        float gFarFade = 0.03f;

		float pad1 = {};
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

    void Commit(double deltaTime) override;

    void ClearDepthMap(ID3D11DeviceContext* ctx);

    void SpawnDrawParticles(ID3D11DeviceContext* ctx, RainParticlePool::TiledLightData* lightData);

    void SetCameraPos(const Math::Vec3f pos) {
        std::lock_guard lock(bufMutex);
        m_cpuSpawnBuffer[currentSlot].gCamPos = pos;
        m_cpuUpdateBuffer[currentSlot].gCamPosWS = pos;
        m_cpuRenderBuffer[currentSlot].gCamPosWS = pos;

        auto depthMapViewProj = MakeDepthMapViewProjNoLock();
        m_cpuMatrixBuffer[currentSlot].gCamViewProj = depthMapViewProj;
    }

    void SetCameraBuffer(const RenderCB& camCB) {
        std::lock_guard lock(bufMutex);
        m_cpuRenderBuffer[currentSlot] = camCB;
    }

    void SetWind(const Math::Vec3f wind) {
        std::lock_guard lock(bufMutex);
        m_cpuUpdateBuffer[currentSlot].gWindWS = wind * m_windPower;
    }

    Math::Matrix4x4f GetDepthMapViewProj(uint32_t slot) const {
        std::lock_guard lock(bufMutex);
        return m_cpuMatrixBuffer[slot].gCamViewProj;
	}

    float GetElapsedTime() const noexcept {
        return m_elapsedTime;
    }

    float GetSpawnRadius() const noexcept {
        return m_spawnRadius;
	}

    void SetSpawnPerFrame(uint32_t count) {
        std::lock_guard lock(bufMutex);
        m_spawnPerFrame = (std::min)(count, RainParticlePool::MaxSpawnPerFrame);
	}

	Graphics::BufferHandle GetSpawnCBHandle() const { return m_spawnCBHandle; }
	Graphics::BufferHandle GetUpdateCBHandle() const { return m_updateCBHandle; }
	Graphics::BufferHandle GetMatrixCBHandle() const { return m_matrixCBHandle; }
	Graphics::BufferHandle GetRenderCBHandle() const { return m_renderCBHandle; }
	Graphics::BufferHandle GetSplashCBHandle() const { return m_wetnessCBHandle; }

	ComPtr<ID3D11Buffer> GetSpawnCB() const { return m_spawnCB; }
	ComPtr<ID3D11Buffer> GetUpdateCB() const { return m_updateCB; }
	ComPtr<ID3D11Buffer> GetMatrixCB() const { return m_matrixCB; }
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
	ComPtr<ID3D11Buffer> m_matrixCB;
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
	Graphics::BufferHandle m_matrixCBHandle;
	Graphics::BufferHandle m_renderCBHandle;
	Graphics::BufferHandle m_wetnessCBHandle;

    mutable std::mutex bufMutex;
    SpawnCB m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	MatrixCB m_cpuMatrixBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
    RenderCB m_cpuRenderBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	WetnessCB m_cpuWetnessBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

    uint32_t currentSlot = 0;
    float m_elapsedTime = 0.0f;

	float m_spawnRadius = 80.0f;

    uint32_t m_spawnPerFrame = 0;//32 << 2;
	float m_windPower = 2.0f;

public:
    STATIC_SERVICE_TAG
};
