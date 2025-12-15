#pragma once


template<typename Partition>
class EnviromentSystem : public ITypeSystem<
	EnviromentSystem<Partition>,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<Audio::AudioService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<>;

	struct AudioPair
	{
		Audio::SoundHandle handle;
		Audio::AudioTicketID ticketID;
	};

public:
	void StartImpl(UndeletablePtr<Audio::AudioService> audioService) {
		//Audio読み込み
		mainBGM.handle = audioService->EnqueueLoadWav("assets/audio/BGM/fjordnosundakaze.mp3");
		Audio::AudioPlayParams bgmPlayParams;
		bgmPlayParams.loop = true;
		bgmPlayParams.volume = 0.8f;
		mainBGM.ticketID = audioService->EnqueuePlay(mainBGM.handle, bgmPlayParams);

		wind.handle = audioService->EnqueueLoadWav("assets/audio/SE/wind_04.mp3");
		Audio::AudioPlayParams windPlayParams;
		windPlayParams.loop = true;
		windPlayParams.volume = 1.5f;
		wind.ticketID = audioService->EnqueuePlay(wind.handle, windPlayParams);
	}

	void EndImpl(UndeletablePtr<Audio::AudioService> audioService) {
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