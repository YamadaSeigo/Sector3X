// FireflyParticlePool.hpp
#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <cstdint>
#include "graphics/D3D11Helpers.h"
#include <SectorFW/Graphics/LightShadowService.h>

struct FireflyParticleGPU
{
    float posWS[3]; float life;
    float velWS[3]; uint32_t volumeSlot;
    float phase; float pad0, pad1, pad2;
};

struct FireflyUpdatePram
{
    float gDamping = 0.5f; // 速度減衰係数
    float gWanderFreq = 1.0f; // ふわふわノイズ周波数
    float gWanderStrength = 10.0f; // ふわふわノイズ強さ
    float gCenterPull = 0.01f; // ボリューム中心への引き戻し強さ
    float gGroundBand = 20.0f; // 地面からの高さ帯
    float gGroundPull = 0.25f; // 地面付近への引き戻し強さ(小さいほうが強)
    float gHeightRange = 15.0f;

    float burstStrength = 8.0f; // 例：3.0
    float burstRadius = 8.0f; // 例：4.0（プレイヤーから何mまで強いか）
    float burstSwirl = 4.5f; // 例：1.5（渦成分）
    float burstUp = 6.0f; // 例：1.0（上方向の押し上げ）

    float gMaxSpeed = 2.0f; // 速度上限
};

class FireflyParticlePool
{
public:
    static constexpr uint32_t MaxParticles = 100000;
    static constexpr uint32_t MaxVolumeSlots = 256;
    static constexpr uint32_t MaxSpawnPerVol = 32;
    static constexpr uint32_t MaxPointLight = 128;

    void Create(ID3D11Device* dev);
    void InitFreeList(ID3D11DeviceContext* ctx, ID3D11Buffer* spawnCB, ID3D11ComputeShader* initCS);

    // Spawn（volumeSRVは前段のFireflyServiceが作ってCommitしているSRV）
    void Spawn(
        ID3D11DeviceContext* ctx,
        ID3D11ComputeShader* spawnCS,
		ID3D11ComputeShader* updateCS,
        ID3D11ComputeShader* argsCS,
        ID3D11ShaderResourceView* volumeSRV,
        ID3D11ShaderResourceView* heightMapSRV,
        ID3D11UnorderedAccessView* pointLightUAV,
        ID3D11Buffer* cbSpawnData,
        ID3D11Buffer* cbTerrain,
        ID3D11Buffer* cbUpdateData,
        ID3D11Buffer* stagingBuf,
        ID3D11VertexShader* vs,
        ID3D11PixelShader* ps,
		ID3D11Buffer* cbCameraData,
        uint32_t activeVolumeCount);

    ID3D11ShaderResourceView* GetParticlesSRV() const { return m_particles.srv.Get(); }
    ID3D11UnorderedAccessView* GetFreeUAV() const { return m_free.uav.Get(); }
    ID3D11UnorderedAccessView* GetVolumeCountUAV() const { return m_volumeCount.uav.Get(); }

    void SetUpdateParam(const FireflyUpdatePram& param) {
        m_cpuUpdateParam = param;
        m_isUpdateParamDirty = true;
	}

private:
    StructuredBufferSRVUAV m_particles;                 // SRV+UAV（RWParticles）
    StructuredBufferSRVUAV m_free;                      // UAV(APPEND) : FreeList
    StructuredBufferSRVUAV m_alivePing, m_alivePong;    // UAV(APPEND) : AliveList
    StructuredBufferSRVUAV m_volumeCount;               // UAV : slotごとの現在数

	RawBufferSRVUAV m_aliveCountRaw;
    RawBufferSRVUAV m_drawArgsRaw;
    RawBufferSRVUAV m_pointLightCount;

	ComPtr<ID3D11Buffer> m_cbUpdateParam;

    ComPtr<ID3D11SamplerState> m_linearSampler;

	FireflyUpdatePram m_cpuUpdateParam;
	bool m_isUpdateParamDirty = true;
};
