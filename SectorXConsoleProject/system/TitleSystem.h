#pragma once

struct CFade
{
	float maxTime = 2.5f; //フェードにかかる時間 // 0.0fにしたらロード時に消す
};

template<typename Partition>
class TitleSystem : public ITypeSystem<
	TitleSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CFade>,
		Write<CColor>
	>,
	//受け取るサービスの指定
	ServiceContext<
		WorldType::RequestService,
		InputService,
		TimerService
	>>{

	using Accessor = ComponentAccessor<Read<CFade>, Write<CColor>>;

public:
	static inline constexpr float LOADED_FADE_TIME = 2.5f;

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<WorldType::RequestService> worldRequestService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<TimerService> timerService) {

		if (eraseSprite.exchange(false, std::memory_order_acq_rel))
		{
			auto& globalEntityManager = partition.GetGlobalEntityManager();

			Query query;
			query.With<CFade, CColor>();
			auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

			auto eraseFunc = [&](Accessor& accessor, size_t entityCount) {

				auto colorComp = accessor.Get<Write<CColor>>();
				auto fadeComp = accessor.Get<Read<CFade>>();
				for (auto i = 0; i < entityCount; ++i)
				{
					// 即座に透明化
					if (fadeComp.value()[i].maxTime == 0.0f)
					{
						colorComp.value()[i].color.a = 0.0f;
					}
				};
				};

			for (ECS::ArchetypeChunk* chunk : chunks)
			{
				Accessor accessor(chunk);
				eraseFunc(accessor, chunk->GetEntityCount());
			}
		}

		if (isLoadedGameLevel.load(std::memory_order_relaxed))
		{
			//フェードはタイムスケールを無視して進める
			fadeTime += timerService->GetUnscaledDeltaTime();

			auto& globalEntityManager = partition.GetGlobalEntityManager();

			Query query;
			query.With<CFade, CColor>();
			auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

			float t = Math::clamp(fadeTime / LOADED_FADE_TIME, 0.0f, 1.0f);

			auto updateFunc = [&](Accessor& accessor, size_t entityCount) {

				auto colorComp = accessor.Get<Write<CColor>>();
				auto fadeComp = accessor.Get<Read<CFade>>();
				for (auto i = 0; i < entityCount; ++i)
				{
					float lt = Math::clamp(fadeTime / fadeComp.value()[i].maxTime, 0.0f, 1.0f);
					colorComp.value()[i].color.a = 1.0f - lt;
				};
				};

			for (ECS::ArchetypeChunk* chunk : chunks)
			{
				Accessor accessor(chunk);
				updateFunc(accessor, chunk->GetEntityCount());
			}

			if (t >= 1.0f)
			{
				//タイトルレベルをクリーンアップ
				auto cleanCmd = worldRequestService->CreateCleanLevelCommand("Title");
				worldRequestService->PushCommand(std::move(cleanCmd));
			}

			return;
		}

		// Enterキーが押されたら、ゲームレベルをロードするリクエストを追加
		if (inputService->IsKeyTrigger(Input::Key::Enter)) {

			auto loadLevelCmd = worldRequestService->CreateLoadLevelCommand("Loading");
			worldRequestService->PushCommand(std::move(loadLevelCmd));

			eraseSprite.store(true, std::memory_order_relaxed);

			//ロード完了後のコールバック
			auto loadedFunc = [&](WorldType::Session* pSession) {

				//ローディングレベルをクリーンアップ
				pSession->CleanLevel("Loading");

				fadeTime = 0.0f;
				isLoadedGameLevel.store(true, std::memory_order_relaxed);
				};

			//ゲームレベルをロードするリクエストを追加

			auto loadGameLevelCmd = worldRequestService->CreateLoadLevelCommand("OpenField", true, true, loadedFunc);
			worldRequestService->PushCommand(std::move(loadGameLevelCmd));
		}
	}
private:
	std::atomic<bool> isLoadedGameLevel = { false };
	std::atomic<bool> eraseSprite = { false };
	float fadeTime = 0.0f;
};
