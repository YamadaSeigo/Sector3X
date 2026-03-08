#pragma once

#include <SectorFW/Debug/UIBus.h>
#include "graphics/RenderDefine.h"
#include "RainService.h"

struct alignas(16) FogCB
{
	// Distance fog
	Math::Vec3f gFogColor = Math::Vec3f(0.8f, 0.8f, 1.0f);
	float gFogStart = 100.0f;
	float gFogEnd = 3000.0f;// (メートル)
	Math::Vec2f _padFog0;
	uint32_t gEnableDistanceFog = 1; // 0/1

	// Height fog
	float gHeightFogBaseHeight = 50.0f; // 霧の基準高さ(この高さ付近が最も濃い想定) 例: 1.0 (地面付近)
	float gHeightFogDensity = 0.01f; // 高さフォグ密度(全体強さ) 例: 0.01
	float gHeightFogFalloff = 0.07f; // 高さ減衰(大きいほど上に行くと急に薄くなる) 例: 0.05
	uint32_t gEnableHeightFog = 1; // 0/1

	// Height fog wind/noise
	Math::Vec2f gFogDisplacementXZ = { 0.0f,0.0f }; // offset (時間経過で変化させる) 例: (0.0, 0.0)
	float  gFogWindSpeed = 0.3f;     // 例: 0.2
	float  gFogNoiseScale = 0.03f;    // 例: 0.08 (ワールド->ノイズ空間)
	float  gFogNoiseAmount = 0.8f;   // 例: 0.35 (濃淡の強さ 0..1)
	float  gFogGroundBand = 20.0f;    // 例: 6.0  (地面付近の厚み)
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

enum class WeatherType : uint32_t
{
	Clear = 0,
	Rain = 1,
};

struct WeatherKey
{
	// Fog
	Math::Vec3f fogColorMul = { 1.0f, 1.0f, 1.0f };
	float fogStartMul = 1.0f;
	float fogEndMul = 1.0f;
	float heightFogDensityAdd = 0.0f;

	// Lighting
	Math::Vec3f ambientColorMul = { 1.0f, 1.0f, 1.0f };
	float ambientIntensityMul = 1.0f;
	Math::Vec3f sunColorMul = { 1.0f, 1.0f, 1.0f };
	float sunIntensityMul = 1.0f;

	// GodRay
	Math::Vec3f godRayTintMul = { 1.0f, 1.0f, 1.0f };
	float godRayIntensityMul = 1.0f;

	// Rain
	float rainSpawnPerFrame = 0.0f;
	float rainAlpha = 0.0f;
	float rainRate = 0.0f;
	float globalWet = 0.0f;
	float dryRate = 0.1f;
	float wetDarken = 0.0f;
	float wetSpecBoost = 0.0f;
	float wetFlatten = 0.0f;
};

struct EnvironmentComposite
{
	TimeOfDayKey tod;

	Math::Vec3f fogColor;
	float fogStart;
	float fogEnd;
	float heightFogDensity;

	Math::Vec3f ambientColor;
	float ambientIntensity;

	Math::Vec3f sunColor;
	float sunIntensity;

	Math::Vec3f godRayTint;
	float godRayIntensity;
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

		constexpr float FAR_CLIP = 1000.0f;

		// Initialize time of day keys
		{
			TimeOfDayKey keys[] = {
				/*			  t,		ambientColor,		ambientIntensity,	fogColor,				fogStart,	fogEnd,			heightFogDensity,	sunColor,		sunIntensity,	godRayTint,		godRayIntensity, emissiveBoost*/
				/*夜明け	*/	{0.0f,   {	0.03f, 0.05f, 0.07f	},		0.3f,	{	0.05f, 0.07f, 0.12f	},	500.0f,		FAR_CLIP * 2.5f,	0.005f,			{	1.0f,0.95f,0.8f	},	1.0f,   {	1.0f,1.0f,1.0f	},	0.0f,			4.0f		},
				/*朝		*/	{0.2f,	 {	0.9f, 0.95f, 1.0f	},		1.0f,	{	1.0f, 0.8f, 0.6f	},	300.0f,		FAR_CLIP * 1.0f,	0.02f,			{	1.0f,0.9f,0.7f	},	2.0f,   {	1.0f,1.0f,0.9f	},	1.0f,			2.0f		},
				/*昼		*/	{0.4f,   {	0.9f, 0.95f, 1.0f	},		1.2f,	{	0.7f, 0.85f, 1.0f	},	100.0f,		FAR_CLIP * 1.5f,	0.002f,			{	1.0f,1.0f,1.0f	},	5.0f,   {	1.0f,1.0f,1.0f	},	0.5f,			1.0f		},
				/*夕方	*/	{0.6f,	 {	1.0f, 0.7f, 0.5f	},		0.7f,	{	0.6f, 0.5f, 0.5f	},	400.0f,		FAR_CLIP * 1.3f,	0.005f,			{	1.0f,0.8f,0.6f	},	2.5f,   {	1.0f,0.9f,0.8f	},	0.8f,			2.5f		},
				/*夜		*/	{0.8f,   {	0.02f, 0.03f, 0.05f	},		0.15f,	{	0.03f, 0.04f, 0.06f	},	600.0f,		FAR_CLIP * 1.7f,	0.0f,			{	1.0f,0.9f,0.7f	},	0.5f,   {	0.8f,0.8f,1.0f	},	0.0f,			4.0f		},
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

		REGISTER_DEBUG_BOUND_SLIDER_FLOAT("TimeOfDay", "timeOfDay", m_timeOfDay, 0.0f, 1000.0f, 0.1f, [&](float value) {
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

		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogBaseHeight, 0.0f, 200.0f, 0.005f);
		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogDensity, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gHeightFogFalloff, 0.0f, 1.0f, 0.001f);

		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseScale, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseAmount, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogGroundBand, 0.0f, 200.0f, 0.02f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseMinHeight, -10.0f, 20.0f, 0.01f);
		BIND_DEBUG_FOG_FLOAT_DATA(gFogNoiseMaxHeight, -10.0f, 20.0f, 0.01f);

		BIND_DEBUG_SLIDER_FLOAT("Fog", "windBaseSpeed", &m_fogWindBaseSpeed, 0.0f, 10.0f, 0.01f);


		REGISTER_DEBUG_CHECKBOX("GodRay", "gEnableGodRay", cpuGodRayBuf.gEnableGodRay, [&](bool value) { isUpdateGodRayBuffer = true; cpuGodRayBuf.gEnableGodRay = value; });

		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayIntensity, 0.0f, 10.0f, 0.005f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayDecay, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayDensity, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayWeight, 0.0f, 0.1f, 0.0001f);
		BIND_DEBUG_GODRAY_FLOAT_DATA(gGodRayMaxDepth, 0.0f, 1.0f, 0.0001f);

		BIND_DEBUG_SLIDER_FLOAT("Weather", "targetRainIntensity", &m_targetRainIntensity, 0.0f, 1.0f, 0.01f);
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

	EnvironmentComposite BuildCompositeEnvironment() const noexcept
	{
		EnvironmentComposite out{};
		out.tod = currentTimeOfDayKey;

		const float rain01 = std::clamp(m_currentRainIntensity, 0.0f, 1.0f);
		const float rainVis = rain01 * rain01; // 見た目系は後半強く
		const float rainLight = rain01;        // 光量は普通に線形
		const float rainFog = rain01;          // fogは基本線形

		out.fogColor = Math::Lerp(
			currentTimeOfDayKey.fogColor,
			currentTimeOfDayKey.fogColor * m_rainWeather.fogColorMul,
			rainFog);

		out.fogStart = std::lerp(
			currentTimeOfDayKey.fogStart,
			currentTimeOfDayKey.fogStart * m_rainWeather.fogStartMul,
			rainFog);

		out.fogEnd = std::lerp(
			currentTimeOfDayKey.fogEnd,
			currentTimeOfDayKey.fogEnd * m_rainWeather.fogEndMul,
			rainFog);

		out.heightFogDensity =
			currentTimeOfDayKey.heightFogDensity +
			m_rainWeather.heightFogDensityAdd * rainFog;

		out.ambientColor = Math::Lerp(
			currentTimeOfDayKey.ambientColor,
			currentTimeOfDayKey.ambientColor * m_rainWeather.ambientColorMul,
			rainLight);

		out.ambientIntensity = std::lerp(
			currentTimeOfDayKey.ambientIntensity,
			currentTimeOfDayKey.ambientIntensity * m_rainWeather.ambientIntensityMul,
			rainLight);

		out.sunColor = Math::Lerp(
			currentTimeOfDayKey.sunColor,
			currentTimeOfDayKey.sunColor * m_rainWeather.sunColorMul,
			rainLight);

		out.sunIntensity = std::lerp(
			currentTimeOfDayKey.sunIntensity,
			currentTimeOfDayKey.sunIntensity * m_rainWeather.sunIntensityMul,
			rainLight);

		out.godRayTint = Math::Lerp(
			currentTimeOfDayKey.godRayTint,
			currentTimeOfDayKey.godRayTint * m_rainWeather.godRayTintMul,
			rainVis);

		out.godRayIntensity = std::lerp(
			currentTimeOfDayKey.godRayIntensity,
			currentTimeOfDayKey.godRayIntensity * m_rainWeather.godRayIntensityMul,
			rainVis);

		return out;
	}

	void PreUpdate(double deltaTime) override {

		slot = (slot + 1) % Graphics::RENDER_BUFFER_COUNT;

		const float dt = static_cast<float>(deltaTime);

		if (isUpdateTimeOfDay)
		{
			m_timeOfDay = std::fmod(m_timeOfDay + dt, m_dayLengthSec);
			CalcCurrentTimeOfDayKey();
		}

		// 雨強度を目標へ漸近
		{
			const float speed = (m_targetRainIntensity > m_currentRainIntensity)
				? m_rainInSpeed
				: m_rainOutSpeed;

			m_currentRainIntensity = Approach(
				m_currentRainIntensity,
				m_targetRainIntensity,
				speed * dt);
		}

		// 時間帯 + 天候合成
		const auto env = BuildCompositeEnvironment();

		// Fog更新
		{
			std::lock_guard lock(updateFogMutex);
			cpuFogBuf.gFogColor = env.fogColor;
			cpuFogBuf.gFogStart = env.fogStart;
			cpuFogBuf.gFogEnd = env.fogEnd;
			cpuFogBuf.gHeightFogDensity = env.heightFogDensity;
			isUpdateFogBuffer = true;
		}

		// GodRay更新
		{
			std::lock_guard lock(updateGodRayMutex);
			cpuGodRayBuf.gGodRayTint = env.godRayTint;
			cpuGodRayBuf.gGodRayIntensity = env.godRayIntensity;
			isUpdateGodRayBuffer = true;
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

	RainWeatherParams BuildRainParams() const noexcept
	{
		RainWeatherParams p{};

		const float rain01 = std::clamp(m_currentRainIntensity, 0.0f, 1.0f);

		// 粒子量は弱雨で少なめ、後半で効くように
		float rainSpawn = rain01 * rain01;

		p.spawnPerFrame = static_cast<uint32_t>(std::lerp(0.0f, 300.0f, rainSpawn));
		p.rainRate = std::lerp(0.0f, 0.8f, rain01);
		p.globalWet = std::lerp(0.0f, 1.0f, rain01);
		p.dryRate = std::lerp(0.12f, 0.01f, rain01);

		p.particleAlpha = std::lerp(0.0f, 0.6f, rain01);
		p.wetDarken = std::lerp(0.0f, 0.8f, rain01);
		p.wetSpecBoost = std::lerp(0.0f, 0.2f, rain01);
		p.wetFlatten = std::lerp(0.0f, 0.7f, rain01);

		p.splashStrength = std::lerp(0.0f, 1.0f, rain01);
		p.splashDensity = std::lerp(0.0f, 0.98f, rain01);
		p.splashScale = std::lerp(0.0f, 3.0f, rain01);

		return p;
	}

	/**
	 * @brief　太陽からの方向を取得
	 */
	Math::Vec3f GetSunDirection() const noexcept {
		return m_sunDirection;
	}

	void SetWindDirSpeed(const Math::Vec2f& dir, float speed, float dt) noexcept {
		std::lock_guard lock(updateFogMutex);
		cpuFogBuf.gFogWindSpeed = m_fogWindBaseSpeed * speed;
		cpuFogBuf.gFogDisplacementXZ += dir * cpuFogBuf.gFogWindSpeed * dt;
		isUpdateFogBuffer = true;
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

	static float Approach(float current, float target, float delta)
	{
		if (current < target) return (std::min)(current + delta, target);
		if (current > target) return (std::max)(current - delta, target);
		return current;
	}

private:
	Graphics::DX11::BufferManager* bufferMgr = nullptr;
	FogCB cpuFogBuf;
	GodRayCB cpuGodRayBuf;

	std::mutex updateFogMutex;
	std::mutex updateGodRayMutex;

	Graphics::BufferHandle fogCBHandle;
	Graphics::BufferHandle godRayCBHandle;

	float m_dayLengthSec = 120.0f; // 一周にかかる時間(秒)
	float m_timeOfDay = 0.0f; // 現在の時間(0.0~1.0)

	float m_fogWindBaseSpeed = 10.0f;

	std::vector<TimeOfDayKey> timeOfDayKeys;
	TimeOfDayKey currentTimeOfDayKey;

	Math::Vec3f m_sunDirection = { 0.0f, -sin(Math::Deg2Rad(START_SUN_ANGLE)) , -cos(Math::Deg2Rad(START_SUN_ANGLE))};

	uint16_t slot = 0;
	bool isUpdateFogBuffer = false;
	bool isUpdateGodRayBuffer = false;

	bool isUpdateTimeOfDay = true;

	WeatherType m_weatherType = WeatherType::Clear;

	float m_currentRainIntensity = 0.0f; // 0..1
	float m_targetRainIntensity = 0.0f; // 0..1

	float m_rainInSpeed = 0.15f; // 秒あたり
	float m_rainOutSpeed = 0.08f; // 晴れる方は少し遅めでもよい

	WeatherKey m_clearWeather{};
	WeatherKey m_rainWeather{
		.fogColorMul = {0.75f, 0.78f, 0.82f},
		.fogStartMul = 0.45f,
		.fogEndMul = 0.55f,
		.heightFogDensityAdd = 0.01f,

		.ambientColorMul = {0.82f, 0.84f, 0.88f},
		.ambientIntensityMul = 0.75f,
		.sunColorMul = {0.85f, 0.88f, 0.92f},
		.sunIntensityMul = 0.35f,

		.godRayTintMul = {0.9f, 0.9f, 0.95f},
		.godRayIntensityMul = 0.1f,

		.rainSpawnPerFrame = 256.0f,
		.rainAlpha = 0.8f,
		.rainRate = 0.8f,
		.globalWet = 1.0f,
		.dryRate = 0.01f,
		.wetDarken = 0.9f,
		.wetSpecBoost = 0.35f,
		.wetFlatten = 0.7f
	};

public:
	STATIC_SERVICE_TAG
};
