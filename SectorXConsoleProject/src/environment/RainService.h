#pragma once

#include "RainParticlePool.h"

struct RainWeatherParams
{
	uint32_t spawnPerFrame = 0;
	float rainRate = 0.0f;
	float globalWet = 0.0f;
	float dryRate = 0.1f;

	float particleAlpha = 0.0f;

	float wetDarken = 0.0f;
	float wetSpecBoost = 0.0f;
	float wetFlatten = 0.0f;

	float splashStrength = 0.0f;
	float splashDensity = 0.0f;
	float splashScale = 0.0f;
};

class RainService : public ECS::IUpdateService, public ECS::ICommitService
{
public:
	constexpr inline static uint32_t DEPTH_MAP_SIZE = 2048;
	constexpr inline static uint32_t WETNESS_MAP_SIZE = 256;

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
		float gLifeFade = 0.4f; // 例: 1.0 (寿命で薄くする強さ)
		float _pad3[2] = {};
	};

	struct WetnessCB
	{
		Math::Vec2f gWetOriginXZ_Snap = {}; // このWetnessRTがカバーするスナップされたワールド原点(XZ),
		float gWetInvWorldSize = 0.0f; // 1 / カバーするワールド幅（例: 1/256m）
		float gWetStrength = 1.0f; // 全体強度

		float gWetDarken = 0.7f; // 濡れで暗くする量(例: 0.35)
		float gWetSpecBoost = 0.1f; // 疑似スペキュラ強度(例: 1.0)
		float gWetFlatten = 0.6f; // 法線コントラスト抑制(例: 0.6)
		float gWetMinNdotUp = 0.2f; // 上面のみ濡らす閾値(例: 0.2)

		Math::Vec2f gInvScreen = {}; // 1/width, 1/height
		Math::Vec2f gProjAB = {}; // Linearize用
		float gEdgeThreshold = 2.0f; // エッジ判定しきい値（線形深度の差）
		float gEdgeSharpness = 0.2f; // マスクの鋭さ
		Math::Vec2f gRainDirSS = { 0.0f, -1.0f }; // スクリーン空間の雨方向（正規化）
		float gTime = 0.0f;
		float gDensity = 0.9f; // 粒密度
		float gStrength = 1.0f; // 合成強度
		float gDotSizeScale = 3.0f;

		float gNearThickZ = 30.0f;
		float gFarThickZ = 46.0f;
		uint32_t gNearRadiusPx = 3;
		uint32_t gFarRadiusPx = 0;

		float gUpMin = 0.2f;
		float gUpMax = 0.8f;
		float gFarFade = 0.02f;

		float gBlurRadiusTexels = 0.0f;
	};

	struct WetnessScrollCB
	{
		int32_t  scrollTexel[2]; // dx, dy
		float    initWetness;

		float    pad1;

		uint32_t texSize[2];     // W,H

		uint32_t pad2[2];
	};

	struct WetnessUpdateCB
	{
		float    dt;
		float    dryRate;
		float    rainRate;
		float    globalWet;
		uint32_t texSize[2];
		float    pad[2];

		// --- ワールド対応（クリップマップ用） ---
		Math::Vec2f gWetOriginXZ; // このWetnessRTの左下(または基準)のworld XZ
		float gWetWorldSize; // 1枚がカバーするワールド幅（正方形, meters）
		float gTimeSec; // 秒（継続的に増える）

		// --- 斑点（雨粒っぽいムラ）パラメータ ---
		float gSpeckleCellSize = 0.01f; // 例: 0.25 (m) 斑点の“セル”サイズ
		float gSpeckleDensity = 5.0f; // 例: 2.0  (大きいほど当たりやすい)
		float gSpeckleAmount = 0.15f; // 例: 0.15 (1回の当たりで足す濡れ量)
		float gSpeckleTimeHz = 20.0f; // 例: 10.0 (1秒に何回パターン更新するか)
	};

	static constexpr uint32_t MaxVolumes = 32;

	RainService(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
		Graphics::DX11::BufferManager* bufferMgr,
		const wchar_t* csInitFreeListPath,
		const wchar_t* csSpawnPath,
		const wchar_t* csUpdatePath,
		const wchar_t* csArgsPath,
		const wchar_t* vsPath,
		const wchar_t* psPath,
		const wchar_t* csWetnessScrollPath,
		const wchar_t* csWetnessUpdatePath);

	void PreUpdate(double deltaTime) override {
		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;
		m_elapsedTime += static_cast<float>(deltaTime);
	}

	void Commit(double deltaTime) override;

	void ClearDepthMap(ID3D11DeviceContext* ctx);

	void SpawnDrawParticles(ID3D11DeviceContext* ctx, RainParticlePool::TiledLightData* lightData);

	void UpdateWetnessCS(ID3D11DeviceContext* ctx);

	void ApplyWeatherParams(const RainWeatherParams& p, uint32_t slot)
	{
		std::lock_guard lock(bufMutex);

		m_spawnPerFrame = p.spawnPerFrame;
		m_rainRate = p.rainRate;
		m_globalWet = p.globalWet;
		m_dryRate = p.dryRate;

		m_cpuRenderBuffer[slot].gAlpha = p.particleAlpha;
		m_cpuWetnessBuffer[slot].gWetDarken = p.wetDarken;
		m_cpuWetnessBuffer[slot].gWetSpecBoost = p.wetSpecBoost;
		m_cpuWetnessBuffer[slot].gWetFlatten = p.wetFlatten;
		m_cpuWetnessBuffer[slot].gStrength = p.splashStrength;
		m_cpuWetnessBuffer[slot].gDensity = p.splashDensity;
		m_cpuWetnessBuffer[slot].gDotSizeScale = p.splashScale;
	}

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

	ComPtr<ID3D11ShaderResourceView> GetWetnessMapSRV() const { return m_WetPrevSRV; }

private:
	Math::Matrix4x4f MakeDepthMapViewProjNoLock() const;

	bool CreateWetnessResources(ID3D11Device* dev, uint32_t w, uint32_t h);
private:
	// ---- GPUリソース ----
	ComPtr<ID3D11Buffer> m_spawnCB;
	ComPtr<ID3D11Buffer> m_updateCB;
	ComPtr<ID3D11Buffer> m_matrixCB;
	ComPtr<ID3D11Buffer> m_renderCB;
	ComPtr<ID3D11Buffer> m_wetnessCB;
	ComPtr<ID3D11Buffer> m_wetScrollCB;
	ComPtr<ID3D11Buffer> m_wetUpdateCB;

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
	Graphics::BufferHandle m_wetScrollCBHandle;
	Graphics::BufferHandle m_wetUpdateCBHandle;

	mutable std::mutex bufMutex;
	SpawnCB m_cpuSpawnBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	UpdateCB m_cpuUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	MatrixCB m_cpuMatrixBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	RenderCB m_cpuRenderBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	WetnessCB m_cpuWetnessBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	WetnessScrollCB m_cpuWetScrollBuffer[Graphics::RENDER_BUFFER_COUNT] = {};
	WetnessUpdateCB m_cpuWetUpdateBuffer[Graphics::RENDER_BUFFER_COUNT] = {};

	uint32_t currentSlot = 0;
	float m_elapsedTime = 0.0f;

	float m_spawnRadius = 80.0f;

	uint32_t m_spawnPerFrame = 0;//32 << 2;
	float m_windPower = 2.0f;

	// 濡れテクスチャ関連
	// Wetness resources
	ComPtr<ID3D11Texture2D> m_WetPrevTex;
	ComPtr<ID3D11Texture2D> m_WetNewTex;
	ComPtr<ID3D11ShaderResourceView> m_WetPrevSRV;
	ComPtr<ID3D11ShaderResourceView> m_WetNewSRV;
	ComPtr<ID3D11UnorderedAccessView> m_WetPrevUAV;
	ComPtr<ID3D11UnorderedAccessView> m_WetNewUAV;

	uint32_t mWetW = 512;
	uint32_t mWetH = 512;
	float    mWetWorldSize = 256.0f; // meters covered by the texture (square)
	Math::Vec2f mWetOriginXZ = { 0,0 }; // world-space origin of the texture

	int32_t mWetOriginTexelX = 0;
	int32_t mWetOriginTexelY = 0;

	float m_initWetnessForNewArea = 0.0f; //スクロールの初期化値
	float m_dryRate = 0.1f;
	float m_rainRate = 0.45f;
	float m_globalWet = 0.0f;

	// CS
	ComPtr<ID3D11ComputeShader> m_wetScrollCopyCS;
	ComPtr<ID3D11ComputeShader> m_wetUpdateCS;

	// Accumulate fractional scroll (optional but recommended)
	Math::Vec2f mScrollRemainder = { 0,0 };

public:
	STATIC_SERVICE_TAG
};
