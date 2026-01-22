// LeafParticlePool.h
#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <cstdint>
#include "D3D11Helpers.h" // StructuredBufferSRVUAV / RawBufferSRVUAV / Create～ 系

// GPU 側の LeafParticle と同じレイアウトにする想定
// HLSL 側の struct LeafParticle と合わせてください。
struct LeafParticleGPU
{
    float posWS[3]; float life;          // 位置 + 残り寿命
    float velWS[3]; uint32_t volumeSlot; // 速度 + 所属 Volume Slot
    float phase; float size;
    uint32_t curveId; // which guide curve
    float s; // 0..1 progress on curve

    float lane; // offset along curve-right (meters)
    float radial; // offset along curve-binormal (meters)
};

// Update 用のパラメータ（FireflyUpdatePram をそのまま流用しても良い）
struct LeafUpdateParam
{
    float gKillRadiusScale = 1.5f; // e.g. 1.5 (kill if dist > radius*scale)
    float gDamping = 0.96f; // e.g. 0.96
    float gFollowK = 12.0f; // e.g. 12.0  (steer strength)
    float gMaxSpeed = 6.0f; // e.g. 6.0

    float gGroundBase = 0.25f; // e.g. 0.25  (meters above ground)
    float gGroundWaveAmp = 10.0f; // e.g. 0.35  (meters)
    float gGroundWaveFreq = 0.8f; // e.g. 0.8   (Hz-ish)
    float gGroundFollowK = 5.0f; // e.g. 2.0   (y position spring)
    float gGroundFollowD = 3.0f; // e.g. 1.2   (y velocity damping)

    float padParams[3] = {};
};

class LeafParticlePool
{
public:
    static constexpr uint32_t MaxParticles = 100000;
    static constexpr uint32_t MaxVolumeSlots = 256;
    static constexpr uint32_t MaxSpawnPerVol = 32;

    // GPUリソース生成
    void Create(ID3D11Device* dev);

    // FreeList 初期化（Firefly と同じ InitFreeList 用CSを使うイメージ）
    void InitFreeList(ID3D11DeviceContext* ctx,
        ID3D11Buffer* spawnCB,
        ID3D11ComputeShader* initCS);

    // 毎フレームの Spawn + Update + Draw
    // FireflyParticlePool::Spawn と同じインターフェース
    void Spawn(
        ID3D11DeviceContext* ctx,
        ID3D11ComputeShader* spawnCS,
        ID3D11ComputeShader* updateCS,
        ID3D11ComputeShader* argsCS,
        ID3D11ShaderResourceView* volumeSRV,
		ID3D11ShaderResourceView* guideCurveSRV,
        ID3D11ShaderResourceView* heightMapSRV,
        ID3D11ShaderResourceView* leafTextureSRV,
        ID3D11Buffer* cbSpawnData,
        ID3D11Buffer* cbTerrain,
        ID3D11Buffer* cbWind,
        ID3D11Buffer* cbUpdateData,
        ID3D11VertexShader* vs,
        ID3D11PixelShader* ps,
        ID3D11Buffer* cbCameraData,
        uint32_t activeVolumeCount);

    // UpdateParam の設定（デバッグGUIなどからいじる用）
    void SetUpdateParam(const LeafUpdateParam& p)
    {
        m_cpuUpdateParam = p;
        m_isUpdateParamDirty = true;
    }

    const LeafUpdateParam& GetUpdateParam() const { return m_cpuUpdateParam; }

    // 必要なら DrawIndirect のバッファ等を外から触れるように
    ID3D11Buffer* GetDrawArgsBuffer() const { return m_drawArgsRaw.buf.Get(); }
    ID3D11ShaderResourceView* GetParticlesSRV() const { return m_particles.srv.Get(); }

private:
    // GPUリソース
    StructuredBufferSRVUAV m_particles;    // RWStructuredBuffer<LeafParticleGPU>
    StructuredBufferSRVUAV m_free;         // AppendStructuredBuffer<uint> : FreeList
    StructuredBufferSRVUAV m_alivePing;    // AppendStructuredBuffer<uint> : 前フレームAlive
    StructuredBufferSRVUAV m_alivePong;    // AppendStructuredBuffer<uint> : 今フレームAlive
    StructuredBufferSRVUAV m_volumeCount;  // RWStructuredBuffer<uint>     : Volumeごとの個数

    RawBufferSRVUAV m_aliveCountRaw;       // alive数カウント用 RawBuffer
    RawBufferSRVUAV m_drawArgsRaw;         // DrawInstancedIndirect 用 Args

    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbUpdateParam;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_linearSampler;

    LeafUpdateParam m_cpuUpdateParam{};
    bool            m_isUpdateParamDirty = true;
};
