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

	Math::Vec3f gGodRayTint = { 1.0f,0.95f,0.5f }; // 色（例: (1.0, 0.95, 0.8)）
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

	Math::Vec3f ambientColor = { 1.0f,1.0f,1.0f };
	float ambientIntensity = 0.0f;

	Math::Vec3f fogColor = { 1.0f,1.0f,1.0f };
	float fogStart = 0.0f, fogEnd = 1.0f;
	float heightFogDensity = 1.0f;

	Math::Vec3f sunColor = { 1.0f,1.0f,1.0f };
	float sunIntensity = 1.0f;

	Math::Vec3f godRayTint = { 1.0f,1.0f,1.0f };
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

enum class WeatherState : uint32_t
{
	Clear = 0,
	Drizzle = 1,
	Rain = 2,
	HeavyRain = 3,
};

struct WeatherTransitionWeights
{
	float toClear = 0.0f;
	float toDrizzle = 0.0f;
	float toRain = 0.0f;
	float toHeavyRain = 0.0f;
};

struct WeatherStateDesc
{
	float minStaySec = 30.0f;
	float maxStaySec = 60.0f;
	float targetRainIntensity = 0.0f;
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

	EnvironmentService(Graphics::DX11::BufferManager* bufferMgr_) noexcept;

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
		if (afterKey.t >= beforeKey.t) {
			factor = (t - beforeKey.t) / (afterKey.t - beforeKey.t);
		}
		else {
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

	void PreUpdate(double deltaTime) override;

	const TimeOfDayKey& GetCurrentTimeOfDayKey() const noexcept {
		return currentTimeOfDayKey;
	}

	RainWeatherParams BuildRainParams() const noexcept;

	void EnableWeatherAutoTransition(bool enable) noexcept {
		m_enableWeatherAutoTransition = enable;
	}

	void EnableWeatherPerlinAssist(bool enable) noexcept {
		m_enableWeatherPerlinAssist = enable;
	}

	void SetRainTargetIntensity(float v) noexcept {
		m_targetRainIntensity = Math::saturate(v);
	}

	float GetRainTargetIntensity() const noexcept {
		return m_targetRainIntensity;
	}

	float GetRainCurrentIntensity() const noexcept {
		return m_currentRainIntensity;
	}

	WeatherState GetWeatherState() const noexcept {
		return m_weatherState;
	}

	void SetWeatherStateImmediate(WeatherState state) noexcept
	{
		m_weatherState = state;
		m_targetRainIntensity = GetTargetRainIntensity(state);
		m_currentRainIntensity = m_targetRainIntensity;
		m_weatherStateElapsed = 0.0f;
		m_nextWeatherDecisionSec = RandomRange(
			GetWeatherStateDesc(state).minStaySec,
			GetWeatherStateDesc(state).maxStaySec);
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

	FogCB GetFogCBData() const noexcept {
		return cpuFogBuf;
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

	WeatherStateDesc GetWeatherStateDesc(WeatherState s) const noexcept
	{
		switch (s)
		{
		default:
		case WeatherState::Clear:
			return { 40.0f, 120.0f, 0.0f };

		case WeatherState::Drizzle:
			return { 20.0f, 60.0f, 0.22f };

		case WeatherState::Rain:
			return { 35.0f, 100.0f, 0.60f };

		case WeatherState::HeavyRain:
			return { 20.0f, 50.0f, 1.0f };
		}
	}

	float GetTargetRainIntensity(WeatherState s) const noexcept
	{
		return GetWeatherStateDesc(s).targetRainIntensity;
	}

	WeatherTransitionWeights GetBaseWeights(WeatherState s) const noexcept
	{
		switch (s)
		{
		default:
		case WeatherState::Clear:
			return { 0.78f, 0.22f, 0.0f, 0.0f };

		case WeatherState::Drizzle:
			return { 0.20f, 0.55f, 0.25f, 0.0f };

		case WeatherState::Rain:
			return { 0.0f, 0.25f, 0.55f, 0.20f };

		case WeatherState::HeavyRain:
			return { 0.0f, 0.0f, 0.68f, 0.32f };
		}
	}

	float Random01() noexcept
	{
		return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
	}

	float RandomRange(float minV, float maxV) noexcept
	{
		return std::uniform_real_distribution<float>(minV, maxV)(m_rng);
	}

	float GetTimeOfDay01() const noexcept
	{
		if (m_dayLengthSec <= 0.0f) return 0.0f;
		return Math::saturate(m_timeOfDay / m_dayLengthSec);
	}

	// 0 = 乾燥寄り, 1 = 湿潤寄り
	float CalcClimateWetness01_RandOnly() const noexcept
	{
		// 最初は固定でも十分。必要ならエリアや季節で差し替える
		return 0.55f;
	}

	// 軽量ハッシュノイズ。Perlin をまだ用意していない場合の代替。
	float HashNoise1D(float x) const noexcept
	{
		float n = std::sin(x * 12.9898f + 78.233f) * 43758.5453f;
		return n - std::floor(n); // frac
	}

	// 補助用。真の Perlin があるなら差し替えてOK
	float SampleWeatherNoise01(float t) const noexcept
	{
		// 長周期 + 少し短周期
		const float n0 = HashNoise1D(t * 0.017f);
		const float n1 = HashNoise1D(t * 0.0047f + 13.1f);
		return Math::saturate(n0 * 0.7f + n1 * 0.3f);
	}

	WeatherState ChooseNextWeatherState_Rand(float timeOfDay01, float climateWetness01) noexcept;

	WeatherState ChooseNextWeatherState_Perlin(float timeOfDay01) noexcept;

	WeatherState ChooseNextWeatherState() noexcept;

	void UpdateWeatherState(float dt) noexcept;

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

	Math::Vec3f m_sunDirection = { 0.0f, -sin(Math::Deg2Rad(START_SUN_ANGLE)) , -cos(Math::Deg2Rad(START_SUN_ANGLE)) };

	uint16_t slot = 0;
	bool isUpdateFogBuffer = false;
	bool isUpdateGodRayBuffer = false;

	bool isUpdateTimeOfDay = true;

	WeatherState m_weatherState = WeatherState::Clear;

	float m_currentRainIntensity = 0.0f; // 0..1
	float m_targetRainIntensity = 0.0f; // 0..1

	float m_rainInSpeed = 0.15f; // 秒あたり
	float m_rainOutSpeed = 0.08f; // 晴れる方は少し遅めでもよい

	float m_rainRiseSpeed = 0.10f; // 雨が強まる速度
	float m_rainFallSpeed = 0.04f; // 雨が弱まる速度

	bool  m_enableWeatherAutoTransition = true;
	bool  m_enableWeatherPerlinAssist = false;

	float m_weatherStateElapsed = 0.0f;
	float m_nextWeatherDecisionSec = 120.0f;

	float m_weatherGlobalClock = 0.0f;

	// rand版の target 小揺れ
	float m_targetWobbleTimer = 0.0f;
	float m_targetWobbleIntervalSec = 8.0f;
	float m_cachedTargetWobble = 0.0f;

	// Perlin補助版の周波数
	float m_weatherClimateFreq = 0.0035f;      // 長周期
	float m_weatherTargetWobbleFreq = 0.025f;  // 状態内揺れ

	std::mt19937 m_rng{ std::random_device{}() };

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
