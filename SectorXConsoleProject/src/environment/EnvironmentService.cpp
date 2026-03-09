
#include "EnvironmentService.h"

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

void EnvironmentService::PreUpdate(double deltaTime)
{
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
