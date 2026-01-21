#pragma once

#include "../app/EnvironmentService.h"

template<typename Partition>
class EnvironmentSystem : public ITypeSystem<
	EnvironmentSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<>,
	//受け取るサービスの指定
	ServiceContext<
		EnvironmentService,
		WindService,
		Graphics::RenderService,
		Graphics::LightShadowService,
		Audio::AudioService,
		Graphics::I3DPerCameraService
	>>
{
	using Accessor = ComponentAccessor<>;

	struct AudioPair
	{
		Audio::SoundHandle handle;
		Audio::AudioTicketID ticketID;
	};

public:
	void StartImpl(
		NoDeletePtr<EnvironmentService> environmentService,
		NoDeletePtr<WindService> grassService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::LightShadowService> lightShadowService,
		NoDeletePtr<Audio::AudioService> audioService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService)
	{
		//Audio読み込み
		mainBGM.handle = audioService->EnqueueLoadWav("assets/audio/BGM/fjordnosundakaze.ogg");
		Audio::AudioPlayParams bgmPlayParams;
		bgmPlayParams.loop = true;
		bgmPlayParams.volume = 0.8f;
		mainBGM.ticketID = audioService->EnqueuePlay(mainBGM.handle, bgmPlayParams);

		wind.handle = audioService->EnqueueLoadWav("assets/audio/SE/wind_04.wav");
		Audio::AudioPlayParams windPlayParams;
		windPlayParams.loop = true;
		windPlayParams.volume = 1.5f;
		wind.ticketID = audioService->EnqueuePlay(wind.handle, windPlayParams);
	}

	void UpdateImpl(
		NoDeletePtr<EnvironmentService> environmentService,
		NoDeletePtr<WindService> grassService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::LightShadowService> lightShadowService,
		NoDeletePtr<Audio::AudioService> audioService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService)
	{
		//草のバッファの更新
		grassService->UpdateBufferToGPU(renderService->GetProduceSlot());

		const auto& timeOfDayKey = environmentService->GetCurrentTimeOfDayKey();
		Math::Vec3f sunDirWS = environmentService->GetSunDirection();

		if (environmentService->IsUpdateTimeOfDay()) {

			lightShadowService->SetEnvironment(
				Graphics::DirectionalLight{
					.directionWS = sunDirWS,
					.color = timeOfDayKey.sunColor,
					.intensity = timeOfDayKey.sunIntensity
				},
				Graphics::AmbientLight{
					.color = timeOfDayKey.ambientColor,
					.intensity = timeOfDayKey.ambientIntensity
				},
				timeOfDayKey.emissiveBoost
			);
		}

		auto camPos = cameraService->GetEyePos();
		const auto& viewProj = cameraService->GetCameraBufferDataNoLock().viewProj;
		const auto& view = cameraService->GetCameraBufferDataNoLock().view;
		Math::Matrix3x3f view3x3 = {
			 view[0][0], view[0][1], view[0][2],
			 view[1][0], view[1][1], view[1][2],
			 view[2][0], view[2][1], view[2][2]
		};

		Math::Vec3f sunPosWS = camPos - sunDirWS * 1000.0f; // 適当に遠くに配置
		Math::Vec4f sunClip = viewProj * Math::Vec4f{ sunPosWS, 1.0f };
		Math::Vec2f sunNDC = sunClip.xy / sunClip.w;
		Math::Vec2f sunUV = sunNDC * 0.5f + 0.5f;

		Math::Vec3f sunDirVS = view3x3 * sunDirWS;
		Math::Vec2f sunDirSS = Math::Vec2f{ sunDirVS.x, -sunDirVS.y }.normalized();

		environmentService->SetSunScreenUVAndDir(sunUV, sunDirSS);
	}

	void EndImpl(
		NoDeletePtr<EnvironmentService> environmentService,
		NoDeletePtr<WindService> grassService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::LightShadowService> lightShadowService,
		NoDeletePtr<Audio::AudioService> audioService,
		NoDeletePtr<Graphics::I3DPerCameraService> cameraService)
	{
		//BGM停止
		if (mainBGM.ticketID.IsValid()) {
			auto voiceIDOpt = audioService->TryResolve(mainBGM.ticketID);
			if (voiceIDOpt.has_value()) {
				audioService->EnqueueStop(voiceIDOpt.value());
			}
		}
		audioService->EnqueueUnload(mainBGM.handle);

		//風音停止
		if (wind.ticketID.IsValid()) {
			auto voiceIDOpt = audioService->TryResolve(wind.ticketID);
			if (voiceIDOpt.has_value()) {
				audioService->EnqueueStop(voiceIDOpt.value());
			}
		}
		audioService->EnqueueUnload(wind.handle);
	}

private:
	AudioPair mainBGM;
	AudioPair wind;
};