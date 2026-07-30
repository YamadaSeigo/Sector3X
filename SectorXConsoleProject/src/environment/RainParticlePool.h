// FireflyParticlePool.hpp
#pragma once
#include <d3d11.h>
#include <cstdint>
#include "graphics/D3D11Helpers.h"

// デバッグ用：雨粒が当たった深度を記録する機能を有効化(シェーダーの方も要対応 _RainParticle.hlsli)
//#define DEBUG_RAIN_HIT_DEPTH

struct RainParticleGPU
{
	float posWS[3];
	float life;
	float velWS[3];
	float addSize; // 加算サイズ

#ifdef DEBUG_RAIN_HIT_DEPTH
	uint32_t debugHit; // デバッグ用：雨粒が当たった深度（0なら当たってない）
#endif
};

class RainParticlePool
{
public:
	static constexpr uint32_t MaxParticles = 100000;
	static constexpr uint32_t MaxSpawnPerFrame = 512; 	// 1フレームあたりの最大スポーン数。これ以上はスポーンされない。

	void Create(ID3D11Device* dev);
	void InitFreeList(ID3D11DeviceContext* ctx, ID3D11Buffer* spawnCB, ID3D11ComputeShader* initCS);

	struct TiledLightData
	{
		ID3D11ShaderResourceView* normalLightSRV;
		ID3D11ShaderResourceView* fireflyLightSRV;
		ID3D11ShaderResourceView* lightCountSRV;
		ID3D11ShaderResourceView* lightIndexSRV;
		ID3D11Buffer* lightCB;
		ID3D11Buffer* tileCB;
	};

	// Spawn（volumeSRVは前段のFireflyServiceが作ってCommitしているSRV）
	void Spawn(
		ID3D11DeviceContext* ctx,
		ID3D11ComputeShader* spawnCS,
		ID3D11ComputeShader* updateCS,
		ID3D11ComputeShader* argsCS,
		ID3D11ShaderResourceView* depthMapSRV,
		ID3D11Buffer* cbSpawnData,
		ID3D11Buffer* cbUpdateData,
		ID3D11Buffer* cbMatrixData,
		ID3D11VertexShader* vs,
		ID3D11PixelShader* ps,
		ID3D11Buffer* cbRenderData,
		TiledLightData* lightData,
		uint32_t frameSpawnCount);

	ID3D11ShaderResourceView* GetParticlesSRV() const { return m_particles.srv.Get(); }
	ID3D11UnorderedAccessView* GetFreeUAV() const { return m_free.uav.Get(); }

private:
	StructuredBufferSRVUAV m_particles;                 // SRV+UAV（RWParticles）
	StructuredBufferSRVUAV m_free;                      // UAV(APPEND) : FreeList
	StructuredBufferSRVUAV m_alivePing, m_alivePong;    // UAV(APPEND) : AliveList

	RawBufferSRVUAV m_aliveCountRaw;
	RawBufferSRVUAV m_drawArgsRaw;
	RawBufferSRVUAV m_particleCounterRaw;

	ComPtr<ID3D11SamplerState> m_pointSampler;
};
