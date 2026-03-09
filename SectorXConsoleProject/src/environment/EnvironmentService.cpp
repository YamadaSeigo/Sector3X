
#include "EnvironmentService.h"
#include <SectorFW/Math/Perlin2D.h>

float gMaxDryRate = 0.15f;
float gMaxRainRate = 0.45f;

EnvironmentService::EnvironmentService(Graphics::DX11::BufferManager* bufferMgr_) noexcept
	: bufferMgr(bufferMgr_)
{
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

	// ---- Weather initial state ----
	m_weatherState = WeatherState::Clear;
	m_currentRainIntensity = 0.0f;
	m_targetRainIntensity = 0.0f;
	m_weatherStateElapsed = 0.0f;
	m_nextWeatherDecisionSec = RandomRange(90.0f, 220.0f);


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

	REGISTER_DEBUG_BOUND_SLIDER_FLOAT("Weather", "currentRainIntensity", m_currentRainIntensity, 0.0f, 1.0f, 0.01f,
		[&](float v) {m_currentRainIntensity = v; }, &m_currentRainIntensity);

	REGISTER_DEBUG_BOUND_SLIDER_FLOAT("Weather", "targetRainIntensity", m_targetRainIntensity, 0.0f, 1.0f, 0.01f,
		[&](float v) {m_targetRainIntensity = v; }, &m_targetRainIntensity);

#ifdef _DEBUG
	BIND_DEBUG_CHECKBOX("Weather", "autoTransition", &m_enableWeatherAutoTransition);
	BIND_DEBUG_CHECKBOX("Weather", "perlinAssist", &m_enableWeatherPerlinAssist);
	BIND_DEBUG_SLIDER_FLOAT("Weather", "rainRiseSpeed", &m_rainRiseSpeed, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Weather", "rainFallSpeed", &m_rainFallSpeed, 0.0f, 1.0f, 0.001f);
	BIND_DEBUG_SLIDER_FLOAT("Weather", "decisionSec", &m_nextWeatherDecisionSec, 1.0f, 300.0f, 0.1f);
#endif
}

void EnvironmentService::PreUpdate(double deltaTime)
{
	slot = (slot + 1) % Graphics::RENDER_BUFFER_COUNT;

	const float dt = static_cast<float>(deltaTime);

	if (isUpdateTimeOfDay)
	{
		m_timeOfDay = std::fmod(m_timeOfDay + dt, m_dayLengthSec);
		CalcCurrentTimeOfDayKey();
	}

	// 天候更新
	UpdateWeatherState(dt);

	m_targetRainIntensity = GetTargetRainIntensity(m_weatherState);

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

RainWeatherParams EnvironmentService::BuildRainParams() const noexcept
{
	RainWeatherParams p{};

	const float rain01 = std::clamp(m_currentRainIntensity, 0.0f, 1.0f);

	// 粒子量は弱雨で少なめ、後半で効くように
	float rainSpawn = rain01 * rain01;

	p.spawnPerFrame = static_cast<uint32_t>(std::lerp(0.0f, 300.0f, rainSpawn));
	p.rainRate = std::lerp(0.0f, gMaxRainRate, rain01);
	p.globalWet = std::lerp(0.0f, 0.4f, rain01);
	p.dryRate = std::lerp(gMaxDryRate, 0.01f, rain01);

	p.particleAlpha = std::lerp(0.0f, 0.6f, rain01);
	p.wetDarken = std::lerp(0.0f, 0.8f, rain01);
	p.wetSpecBoost = std::lerp(0.0f, 0.2f, rain01);
	p.wetFlatten = std::lerp(0.0f, 0.7f, rain01);

	p.splashStrength = std::lerp(0.0f, 1.0f, rain01);
	p.splashDensity = std::lerp(0.0f, 0.98f, rain01);
	p.splashScale = std::lerp(0.0f, 3.0f, rain01);

	return p;
}

WeatherState EnvironmentService::ChooseNextWeatherState_Rand(float timeOfDay01, float climateWetness01) noexcept
{
	auto w = GetBaseWeights(m_weatherState);

	// 時間帯補正
	const bool nightLike = (timeOfDay01 < 0.20f || timeOfDay01 > 0.70f);
	const bool noonLike = (timeOfDay01 > 0.33f && timeOfDay01 < 0.52f);

	const float rainBiasBase = Math::lerp(0.65f, 1.35f, climateWetness01);
	const float clearBiasBase = Math::lerp(1.35f, 0.65f, climateWetness01);

	float drizzleBias = 1.0f;
	float rainBias = rainBiasBase;
	float heavyBias = rainBiasBase;
	float clearBias = clearBiasBase;

	if (nightLike)
	{
		rainBias *= 1.10f;
		heavyBias *= 1.15f;
		drizzleBias *= 1.05f;
	}

	if (noonLike)
	{
		heavyBias *= 0.85f;
		clearBias *= 1.08f;
	}

	w.toClear *= clearBias;
	w.toDrizzle *= drizzleBias;
	w.toRain *= rainBias;
	w.toHeavyRain *= heavyBias;

	const float sum = w.toClear + w.toDrizzle + w.toRain + w.toHeavyRain;
	if (sum <= 1e-6f)
		return m_weatherState;

	const float r = Random01() * sum;

	float acc = 0.0f;
	acc += w.toClear;     if (r <= acc) return WeatherState::Clear;
	acc += w.toDrizzle;   if (r <= acc) return WeatherState::Drizzle;
	acc += w.toRain;      if (r <= acc) return WeatherState::Rain;
	return WeatherState::HeavyRain;
}

WeatherState EnvironmentService::ChooseNextWeatherState_Perlin(float timeOfDay01) noexcept
{
	auto w = GetBaseWeights(m_weatherState);

	// 長周期の湿潤度
	const float climateWetness01 = SampleWeatherNoise01(m_weatherGlobalClock * m_weatherClimateFreq);

	// 時間帯補正
	const bool nightLike = (timeOfDay01 < 0.20f || timeOfDay01 > 0.70f);
	const bool noonLike = (timeOfDay01 > 0.33f && timeOfDay01 < 0.52f);

	const float rainBiasBase = Math::lerp(0.60f, 1.45f, climateWetness01);
	const float clearBiasBase = Math::lerp(1.40f, 0.60f, climateWetness01);

	float drizzleBias = 1.0f;
	float rainBias = rainBiasBase;
	float heavyBias = rainBiasBase;
	float clearBias = clearBiasBase;

	if (nightLike)
	{
		drizzleBias *= 1.05f;
		rainBias *= 1.10f;
		heavyBias *= 1.18f;
	}

	if (noonLike)
	{
		heavyBias *= 0.82f;
		clearBias *= 1.08f;
	}

	w.toClear *= clearBias;
	w.toDrizzle *= drizzleBias;
	w.toRain *= rainBias;
	w.toHeavyRain *= heavyBias;

	const float sum = w.toClear + w.toDrizzle + w.toRain + w.toHeavyRain;
	if (sum <= 1e-6f)
		return m_weatherState;

	const float r = Random01() * sum;

	float acc = 0.0f;
	acc += w.toClear;     if (r <= acc) return WeatherState::Clear;
	acc += w.toDrizzle;   if (r <= acc) return WeatherState::Drizzle;
	acc += w.toRain;      if (r <= acc) return WeatherState::Rain;
	return WeatherState::HeavyRain;
}

WeatherState EnvironmentService::ChooseNextWeatherState() noexcept
{
	const float timeOfDay01 = GetTimeOfDay01();

	if (m_enableWeatherPerlinAssist)
	{
		return ChooseNextWeatherState_Perlin(timeOfDay01);
	}

	return ChooseNextWeatherState_Rand(
		timeOfDay01,
		CalcClimateWetness01_RandOnly());
}

void EnvironmentService::UpdateWeatherState(float dt) noexcept
{
	m_weatherGlobalClock += dt;
	m_weatherStateElapsed += dt;

	if (m_enableWeatherAutoTransition &&
		m_weatherStateElapsed >= m_nextWeatherDecisionSec)
	{
		m_weatherState = ChooseNextWeatherState();
		m_weatherStateElapsed = 0.0f;

		const auto desc = GetWeatherStateDesc(m_weatherState);
		m_nextWeatherDecisionSec = RandomRange(desc.minStaySec, desc.maxStaySec);
	}

	// 基本 target
	float baseTarget = GetTargetRainIntensity(m_weatherState);

	// 状態内の小さな揺らぎ
	float targetOffset = 0.0f;
	if (m_enableWeatherPerlinAssist)
	{
		// 強い状態ほど少し揺らす
		const float wobbleAmp =
			(m_weatherState == WeatherState::Clear) ? 0.02f :
			(m_weatherState == WeatherState::Drizzle) ? 0.05f :
			(m_weatherState == WeatherState::Rain) ? 0.08f :
			0.10f;

		const float wobble01 = SampleWeatherNoise01(m_weatherGlobalClock * m_weatherTargetWobbleFreq + 37.0f);
		targetOffset = (wobble01 - 0.5f) * (wobbleAmp * 2.0f);
	}
	else
	{
		// rand版でも急に値を振らないよう、低頻度更新で十分
		m_targetWobbleTimer += dt;
		if (m_targetWobbleTimer >= m_targetWobbleIntervalSec)
		{
			m_targetWobbleTimer = 0.0f;
			m_cachedTargetWobble = RandomRange(-0.04f, 0.04f);
		}
		targetOffset = m_cachedTargetWobble;
	}

	m_targetRainIntensity = Math::saturate(baseTarget + targetOffset);

	// current は target に追従
	const float speed =
		(m_currentRainIntensity < m_targetRainIntensity)
		? m_rainRiseSpeed
		: m_rainFallSpeed;

	m_currentRainIntensity = Approach(
		m_currentRainIntensity,
		m_targetRainIntensity,
		speed * dt);
}
