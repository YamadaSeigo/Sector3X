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

struct TimeOfDayKey
{
	float t = 0.0f; // 0..1

	Math::Vec3f ambientColor = {1.0f,1.0f,1.0f};
	float ambientIntensity = 0.0f;

	Math::Vec3f fogColor = {1.0f,1.0f,1.0f};
	float fogStart = 0.0f, fogEnd = 1.0f;
	float heightFogDensity = 1.0f;

	Math::Vec3f sunColor = {1.0f,1.0f,1.0f};
	float sunIntensity = 1.0f;

	Math::Vec3f godRayTint = {1.0f,1.0f,1.0f};
	float godRayIntensity = 1.0f;

	float emissiveBoost = 3.0f;

	TimeOfDayKey Lerp(const TimeOfDayKey& other, float factor) const noexcept {
		TimeOfDayKey result;
		result.t = std::lerp(t, other.t, factor);
		result.ambientColor = Math::Lerp(ambientColor, other.ambientColor, factor);
		result.ambientIntensity = std::lerp(ambientIntensity, other.ambientIntensity, factor);
		result.fogColor = Math::Lerp(fogColor, other.fogColor, factor);
		result.fogStart = std::lerp(fogStart, other.fogStart, factor);
		result.fogEnd = std::lerp(fogEnd, other.fogEnd, factor);
		result.heightFogDensity = std::lerp(heightFogDensity, other.heightFogDensity, factor);
		result.sunColor = Math::Lerp(sunColor, other.sunColor, factor);
		result.sunIntensity = std::lerp(sunIntensity, other.sunIntensity, factor);
		result.godRayTint = Math::Lerp(godRayTint, other.godRayTint, factor);
		result.godRayIntensity = std::lerp(godRayIntensity, other.godRayIntensity, factor);
		result.emissiveBoost = std::lerp(emissiveBoost, other.emissiveBoost, factor);
		return result;
	}

	bool operator<(const TimeOfDayKey& other) const noexcept {
		return t < other.t;
	}

	// 必要なら他にも (gFogNoiseAmount, gFogGroundBand など)
};

class EnvironmentService : public ECS::IUpdateService
{
public:
	static constexpr inline const char* FOG_BUFFER_NAME = "FogCB";
	static constexpr inline const char* GODRAY_BUFFER_NAME = "GodRayCB";

	static constexpr inline float START_SUN_ANGLE = -72.0f;
	static constexpr inline float END_SUN_ANGLE = 240.0f;

	EnvironmentService(Graphics::DX11::BufferManager* bufferMgr_) noexcept
		: bufferMgr(bufferMgr_) {

		using namespace Graphics;

		// Initialize time of day keys
		{
			TimeOfDayKey keys[] = {
				/*			  t,		ambientColor,		ambientIntensity,	fogColor,				fogStart,	fogEnd,		heightFogDensity,	sunColor,		sunIntensity,	godRayTint,		godRayIntensity, emissiveBoost*/
				/*夜明け	*/	{0.0f,   {	0.03f, 0.05f, 0.07f	},		0.3f,	{	0.05f, 0.07f, 0.12f	},	500.0f,		2500.0f,	0.005f,			{	1.0f,0.95f,0.8f	},	1.0f,   {	1.0f,1.0f,1.0f	},	0.0f,			4.0f		},
				/*朝		*/	{0.2f,	 {	0.9f, 0.95f, 1.0f	},		1.0f,	{	1.0f, 0.8f, 0.6f	},	300.0f,		1500.0f,	0.02f,			{	1.0f,0.9f,0.7f	},	2.0f,   {	1.0f,1.0f,0.9f	},	1.0f,			2.0f		},
				/*昼		*/	{0.4f,   {	0.9f, 0.95f, 1.0f	},		1.2f,	{	0.7f, 0.85f, 1.0f	},	100.0f,		2000.0f,	0.002f,			{	1.0f,1.0f,1.0f	},	5.0f,   {	1.0f,1.0f,1.0f	},	0.5f,			1.0f		},
				/*夕方	*/	{0.6f,	 {	1.0f, 0.7f, 0.5f	},		0.7f,	{	0.6f, 0.5f, 0.5f	},	400.0f,		1800.0f,	0.005f,			{	1.0f,0.8f,0.6f	},	2.5f,   {	1.0f,0.9f,0.8f	},	0.8f,			2.5f		},
				/*夜		*/	{0.8f,   {	0.02f, 0.03f, 0.05f	},		0.15f,	{	0.03f, 0.04f, 0.06f	},	600.0f,		2200.0f,	0.0f,			{	1.0f,0.9f,0.7f	},	0.5f,   {	0.8f,0.8f,1.0f	},	0.0f,			4.0f		},
			};

			timeOfDayKeys.assign(std::begin(keys), std::end(keys));
			std::sort(timeOfDayKeys.begin(), timeOfDayKeys.end());
		}

		CalcCurrentTimeOfDayKey();

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

		BIND_DEBUG_CHECKBOX("TimeOfDay", "enable", &isUpdateTimeOfDay);
		BIND_DEBUG_SLIDER_FLOAT("TimeOfDay", "dayLengthSec", &m_dayLengthSec, m_dayLengthSec, 1000.0f, 1.0f);

		REGISTER_DEBUG_BOUND_SLIDER_FLOAT("TimeOfDay", "timeOfDay", m_timeOfDay, 0.0f, m_dayLengthSec, 0.1f, [&](float value) {
			m_timeOfDay = std::fmod(value, m_dayLengthSec);
			CalcCurrentTimeOfDayKey();
			// Update fog parameters
			{
				std::lock_guard lock(updateFogMutex);
				cpuFogBuf.gFogColor = currentTimeOfDayKey.fogColor;
				cpuFogBuf.gFogStart = currentTimeOfDayKey.fogStart;
				cpuFogBuf.gFogEnd = currentTimeOfDayKey.fogEnd;
				cpuFogBuf.gHeightFogDensity = currentTimeOfDayKey.heightFogDensity;
				isUpdateFogBuffer = true;
			}
			// Update god ray parameters
			{
				std::lock_guard lock(updateGodRayMutex);
				cpuGodRayBuf.gGodRayTint = currentTimeOfDayKey.godRayTint;
				cpuGodRayBuf.gGodRayIntensity = currentTimeOfDayKey.godRayIntensity;
				isUpdateGodRayBuffer = true;
			}
			}, &m_timeOfDay);

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

	void CalcCurrentTimeOfDayKey() noexcept {
		float t = m_timeOfDay / m_dayLengthSec;
		TimeOfDayKey beforeKey = timeOfDayKeys.back();
		TimeOfDayKey afterKey = timeOfDayKeys.front();
		for (size_t i = 0; i < timeOfDayKeys.size(); ++i)
		{
			if (timeOfDayKeys[i].t >= t)
			{
				afterKey = timeOfDayKeys[i];
				beforeKey = (i == 0) ? timeOfDayKeys.back() : timeOfDayKeys[i - 1];
				break;
			}
		}
		float factor = 0.0f;
		if (afterKey.t >= beforeKey.t){
			factor = (t - beforeKey.t) / (afterKey.t - beforeKey.t);
		}
		else{
			// ループしている場合
			factor = (t - beforeKey.t) / (afterKey.t + 1.0f - beforeKey.t);
		}
		currentTimeOfDayKey = beforeKey.Lerp(afterKey, factor);

		// SunDirectionも更新
		float theta = Math::Deg2Rad(START_SUN_ANGLE + (END_SUN_ANGLE - START_SUN_ANGLE) * t);
		m_sunDirection = Math::Vec3f{ 0.0f, -sin(theta), -cos(theta) }.normalized();
	}

	void PreUpdate(double deltaTime) override {

		slot = (slot + 1) % Graphics::RENDER_BUFFER_COUNT;

		if (isUpdateTimeOfDay)
		{
			m_elapsedTime += static_cast<float>(deltaTime);
			m_timeOfDay = std::fmod(m_elapsedTime, m_dayLengthSec);

			CalcCurrentTimeOfDayKey();

			// Update fog parameters
			{
				std::lock_guard lock(updateFogMutex);
				cpuFogBuf.gFogColor = currentTimeOfDayKey.fogColor;
				cpuFogBuf.gFogStart = currentTimeOfDayKey.fogStart;
				cpuFogBuf.gFogEnd = currentTimeOfDayKey.fogEnd;
				cpuFogBuf.gHeightFogDensity = currentTimeOfDayKey.heightFogDensity;
				isUpdateFogBuffer = true;
			}
			// Update god ray parameters
			{
				std::lock_guard lock(updateGodRayMutex);
				cpuGodRayBuf.gGodRayTint = currentTimeOfDayKey.godRayTint;
				cpuGodRayBuf.gGodRayIntensity = currentTimeOfDayKey.godRayIntensity;
				isUpdateGodRayBuffer = true;
			}
		}

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

	const TimeOfDayKey& GetCurrentTimeOfDayKey() const noexcept {
		return currentTimeOfDayKey;
	}

	/**
	 * @brief　太陽からの方向を取得
	 */
	Math::Vec3f GetSunDirection() const noexcept {
		return m_sunDirection;
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

	bool IsUpdateTimeOfDay() const noexcept {
		return isUpdateTimeOfDay;
	}
private:
	void PushTimeOfDayKey(const TimeOfDayKey& key) noexcept {
		timeOfDayKeys.push_back(key);
		std::sort(timeOfDayKeys.begin(), timeOfDayKeys.end());
	}

private:
	Graphics::DX11::BufferManager* bufferMgr = nullptr;
	FogCB cpuFogBuf;
	GodRayCB cpuGodRayBuf;

	std::mutex updateFogMutex;
	std::mutex updateGodRayMutex;

	Graphics::BufferHandle fogCBHandle;
	Graphics::BufferHandle godRayCBHandle;

	float m_elapsedTime = 0.0f;
	float m_dayLengthSec = 120.0f; // 一周にかかる時間(秒)
	float m_timeOfDay = 0.0f; // 現在の時間(0.0~1.0)

	std::vector<TimeOfDayKey> timeOfDayKeys;
	TimeOfDayKey currentTimeOfDayKey;

	Math::Vec3f m_sunDirection = { 0.0f, -sin(Math::Deg2Rad(START_SUN_ANGLE)) , -cos(Math::Deg2Rad(START_SUN_ANGLE))};

	uint16_t slot = 0;
	bool isUpdateFogBuffer = false;
	bool isUpdateGodRayBuffer = false;

	bool isUpdateTimeOfDay = true;

public:
	STATIC_SERVICE_TAG
};
