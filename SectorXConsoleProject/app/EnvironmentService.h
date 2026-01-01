#pragma once

struct alignas(16) FogCB
{
	// Distance fog
	Math::Vec3f gFogColor = Math::Vec3f(0.8f, 0.8f, 1.0f);
	float gFogStart = 100.0f;
	float gFogEnd = 3000.0f;// (メートル)
	Math::Vec2f _padFog0;
	uint32_t gEnableDistanceFog = 1; // 0/1

	// Height fog
	float gHeightFogBaseHeight = 1.0f; // 霧の基準高さ(この高さ付近が最も濃い想定) 例: 1.0 (地面付近)
	float gHeightFogDensity = 0.01f; // 高さフォグ密度(全体強さ) 例: 0.01
	float gHeightFogFalloff = 0.07f; // 高さ減衰(大きいほど上に行くと急に薄くなる) 例: 0.05
	uint32_t gEnableHeightFog = 1; // 0/1

	// Height fog wind/noise
	Math::Vec2f gFogWindDirXZ = {1.0f,0.0f};     // 正規化推奨 (x,z)
	float  gFogWindSpeed = 0.3f;     // 例: 0.2
	float  gFogNoiseScale = 0.01f;    // 例: 0.08 (ワールド->ノイズ空間)
	float  gFogNoiseAmount = 0.8f;   // 例: 0.35 (濃淡の強さ 0..1)
	float  gFogGroundBand = 8.0f;    // 例: 6.0  (地面付近の厚み)
	float  gFogNoiseMinHeight = -1.0f;// 例: -1.0 (基準高さから下は強め等)
	float  gFogNoiseMaxHeight = 8.0f;// 例: 8.0  (基準高さから上は減衰)
};

struct GodRayCB
{
	Math::Vec2f gSunScreenUV = {}; // 太陽のスクリーンUV(0..1) ※CPUで計算して渡す
	float gGodRayIntensity = 0.6f; // 強さ（例: 0.6）
	float gGodRayDecay = 0.96f; // 減衰（例: 0.96）

	Math::Vec2f gSunDirSS = {}; // 太陽のスクリーン方向ベクトル（正規化済み、スクリーン中心から太陽への方向）
	float _padGR1[2] = {};

	float gGodRayDensity = 0.9f; // 伸び具合（例: 0.9）
	float gGodRayWeight = 0.02f; // サンプル重み（例: 0.02）
	uint32_t gEnableGodRay = 1; // 0/1
	float _padGR0 = {};

	Math::Vec3f gGodRayTint = {1.0f,0.95f,0.5f}; // 色（例: (1.0, 0.95, 0.8)）
	float gGodRayMaxDepth = 0.9995f; // “空/遠方”判定の深度閾値（例: 0.9995）
};

#define BIND_DEBUG_FOG_FLOAT_DATA(var, min, max, speed)\
REGISTER_DEBUG_SLIDER_FLOAT("Fog", #var, cpuFogBuf.##var, ##min, ##max, speed, [&](float value){\
	isUpdateFogBuffer= true;cpuFogBuf.##var = value;})

#define BIND_DEBUG_GODRAY_FLOAT_DATA(var, min, max, speed)\
REGISTER_DEBUG_SLIDER_FLOAT("GodRay", #var, cpuGodRayBuf.##var, ##min, ##max, speed, [&](float value){\
	isUpdateGodRayBuffer= true;cpuGodRayBuf.##var = value;})

class EnvironmentService : public ECS::IUpdateService
{
public:
	static constexpr inline const char* FOG_BUFFER_NAME = "FogCB";
	static constexpr inline const char* GODRAY_BUFFER_NAME = "GodRayCB";

	EnvironmentService(Graphics::DX11::BufferManager* bufferMgr_) noexcept
		: bufferMgr(bufferMgr_) {

		using namespace Graphics;

		DX11::BufferCreateDesc fogCBDesc;
		fogCBDesc.name = FOG_BUFFER_NAME;
		fogCBDesc.size = sizeof(FogCB);
		fogCBDesc.initialData = &cpuFogBuf;
		bufferMgr->Add(fogCBDesc, fogCBHandle);

		DX11::BufferCreateDesc godRayCBDesc;
		godRayCBDesc.name = GODRAY_BUFFER_NAME;
		godRayCBDesc.size = sizeof(GodRayCB);
		godRayCBDesc.initialData = &cpuGodRayBuf;
		bufferMgr->Add(godRayCBDesc, godRayCBHandle);

		REGISTER_DEBUG_CHECKBOX("Fog", "gEnableDistanceFog", cpuFogBuf.gEnableDistanceFog, [&](bool value) { isUpdateFogBuffer = true; cpuFogBuf.gEnableDistanceFog = value; });

		REGISTER_DEBUG_CHECKBOX("Fog", "gEnableHeightFog", cpuFogBuf.gEnableHeightFog, [&](bool value) { isUpdateFogBuffer = true; cpuFogBuf.gEnableHeightFog = value; });

		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogBaseHeight, 0.0f, 10.0f, 0.005f);
		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogDensity, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogFalloff, 0.0f, 1.0f, 0.001f);

		BIND_DEBUG_FOG_FLOAT_DATA(gFogWindSpeed, 0.0f, 10.0f, 0.005f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseScale, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseAmount, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogGroundBand, 0.0f, 20.0f, 0.02f);


		REGISTER_DEBUG_CHECKBOX("GodRay", "gEnableGodRay", cpuGodRayBuf.gEnableGodRay, [&](bool value) { isUpdateGodRayBuffer = true; cpuGodRayBuf.gEnableGodRay = value; });

		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayIntensity, 0.0f, 10.0f, 0.005f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayDecay, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayDensity, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayWeight, 0.0f, 0.1f, 0.0001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayMaxDepth, 0.0f, 1.0f, 0.0001f);
	}

	void PreUpdate(double deltaTime) override {

		slot = (slot + 1) % Graphics::RENDER_BUFFER_COUNT;

		if (isUpdateFogBuffer)
		{
			isUpdateFogBuffer = false;

			// 定数バッファ更新
			using namespace Graphics;
			DX11::BufferUpdateDesc updateDesc;
			auto fogCBData = bufferMgr->Get(fogCBHandle);

			updateDesc.buffer = fogCBData.ref().buffer;
			updateDesc.data = &cpuFogBuf;
			updateDesc.size = sizeof(FogCB);
			updateDesc.isDelete = false;
			bufferMgr->UpdateBuffer(updateDesc, slot);
		}

		if (isUpdateGodRayBuffer)
		{
			isUpdateGodRayBuffer = false;
			// 定数バッファ更新
			using namespace Graphics;
			DX11::BufferUpdateDesc updateDesc;
			auto godRayCBData = bufferMgr->Get(godRayCBHandle);
			updateDesc.buffer = godRayCBData.ref().buffer;
			updateDesc.data = &cpuGodRayBuf;
			updateDesc.size = sizeof(GodRayCB);
			updateDesc.isDelete = false;
			bufferMgr->UpdateBuffer(updateDesc, slot);
		}
	}

	void SetSunScreenUVAndDir(const Math::Vec2f& uv, const Math::Vec2f& dir) noexcept {
		std::lock_guard lock(updateGodRayMutex);
		cpuGodRayBuf.gSunScreenUV = uv;
		cpuGodRayBuf.gSunDirSS = dir;
		isUpdateGodRayBuffer = true;
	}

	const Graphics::BufferHandle& GetFogCBHandle() const noexcept {
		return fogCBHandle;
	}

private:
	Graphics::DX11::BufferManager* bufferMgr = nullptr;
	FogCB cpuFogBuf;
	GodRayCB cpuGodRayBuf;

	std::mutex updateFogMutex;
	std::mutex updateGodRayMutex;

	Graphics::BufferHandle fogCBHandle;
	Graphics::BufferHandle godRayCBHandle;

	uint16_t slot = 0;
	bool isUpdateFogBuffer = false;
	bool isUpdateGodRayBuffer = false;

public:
	STATIC_SERVICE_TAG
};
