// LeafParticlePool.h
#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <cstdint>
#include "graphics/D3D11Helpers.h" // StructuredBufferSRVUAV / RawBufferSRVUAV / Create～ 系

//#define DEBUG_DEPTH_HIT

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

    float life0; // 初期寿命(sec)
    float tint[3]; // 葉っぱ固有色

#ifdef DEBUG_DEPTH_HIT
    float depthHit; // デバッグ用: 深度ヒット回数カウント
#endif
};

// Update 用のパラメータ（FireflyUpdatePram をそのまま流用しても良い）
struct LeafUpdateParam
{
    float gKillRadiusScale = 1.5f; // 例: 1.5
    float gDamping = 0.96f; // 例: 0.96（multiply）
    float gFollowK = 12; // 例: 6..14
    float gMaxSpeed = 8.0f; // 例: 6

    // ground params（Update側でも使うなら）
    float gGroundMinClear = 0.05f; // 例: 0.05（地面押し上げ最小クリア）
    float _padA = {}, _padB = {}, _padC = {};
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
        ID3D11ComputeShader* clumpCS,
        ID3D11ComputeShader* spawnCS,
        ID3D11ComputeShader* updateCS,
        ID3D11ComputeShader* argsCS,
        ID3D11ShaderResourceView* volumeSRV,
		ID3D11ShaderResourceView* guideCurveSRV,
        ID3D11ShaderResourceView* clumpSRV,
        ID3D11ShaderResourceView* heightMapSRV,
        ID3D11ShaderResourceView* leafTextureSRV,
        ID3D11ShaderResourceView* depthSRV,
		ID3D11UnorderedAccessView* clumpUAV,
        ID3D11Buffer* cbClumpUpdate,
        ID3D11Buffer* cbSpawnData,
        ID3D11Buffer* cbTerrain,
        ID3D11Buffer* cbWind,
        ID3D11Buffer* cbUpdateData,
        ID3D11Buffer* cbCameraData,
        ID3D11VertexShader* vs,
        ID3D11PixelShader* ps,
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
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_pointSampler;

    LeafUpdateParam m_cpuUpdateParam{};
    bool            m_isUpdateParamDirty = true;
};
