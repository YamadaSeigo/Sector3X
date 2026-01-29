#pragma once

#include "app/appconfig.h"
#include "system/PlayerSystem.h"

struct CTitleSprite
{
	float fadeTime = 2.5f; //フェードにかかる時間
	bool isErased = false;
};

template<typename Partition>
class TitleSystem : public ITypeSystem<
	TitleSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CTitleSprite>,
		Write<CColor>
	>,
	//受け取るサービスの指定
	ServiceContext<
		WorldType::RequestService,
		InputService,
		TimerService
	>>{

	using Accessor = ComponentAccessor<Read<CTitleSprite>, Write<CColor>>;

public:
	//ロード完了してからタイトルを見せるまで待つ時間
	static inline constexpr float WAIT_FADE_TIME = 0.0f;
	//Enterを押してからゲームが始まる時間
	static inline constexpr float START_GAME_TIME = 2.5f;

	void StartImpl(
		NoDeletePtr<WorldType::RequestService> worldRequestService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<TimerService> timerService) {

		//ローディング中のレベルを先にロード
		{
			auto loadCmd = worldRequestService->CreateLoadLevelCommand(App::LOADING_LEVEL_NAME);
			worldRequestService->PushCommand(std::move(loadCmd));
		}

		{
			//ロード完了後のコールバック
			auto loadedFunc = [&](WorldType::Session* pSession) {

				//ローディングレベルをクリーンアップ
				pSession->CleanLevel(App::LOADING_LEVEL_NAME);

				loadedMainLevel.store(true, std::memory_order_relaxed);
				};

			auto loadCmd = worldRequestService->CreateLoadLevelCommand(App::MAIN_LEVEL_NAME, true, true, loadedFunc);
			worldRequestService->PushCommand(std::move(loadCmd));
		}
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<WorldType::RequestService> worldRequestService,
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<TimerService> timerService) {

		bool isLoadedMainLevel = loadedMainLevel.load(std::memory_order_relaxed);

		if (isLoadedMainLevel)
		{
			//フェードはタイムスケールを無視して進める
			fadeElapsedTime += timerService->GetUnscaledDeltaTime();

			auto& globalEntityManager = partition.GetGlobalEntityManager();

			Query query;
			query.With<CTitleSprite, CColor>();
			auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

			auto updateFunc = [&](Accessor& accessor, size_t entityCount) {

				auto colorComp = accessor.Get<Write<CColor>>();
				auto titleComp = accessor.Get<Read<CTitleSprite>>();
				for (auto i = 0; i < entityCount; ++i)
				{
					auto tc = titleComp.value()[i];
					if (tc.isErased) {
						float lt = Math::clamp((fadeElapsedTime - WAIT_FADE_TIME) / tc.fadeTime, 0.0f, 1.0f);
						colorComp.value()[i].color.a = 1.0f - lt;
					}
				};
				};

			for (ECS::ArchetypeChunk* chunk : chunks)
			{
				Accessor accessor(chunk);
				updateFunc(accessor, chunk->GetEntityCount());
			}
		}

		if (startGame)
		{
			//フェードはタイムスケールを無視して進める
			fadeElapsedTime += timerService->GetUnscaledDeltaTime();

			auto& globalEntityManager = partition.GetGlobalEntityManager();

			Query query;
			query.With<CTitleSprite, CColor>();
			auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

			float t = Math::clamp(fadeElapsedTime / START_GAME_TIME, 0.0f, 1.0f);

			auto updateFunc = [&](Accessor& accessor, size_t entityCount) {

				auto colorComp = accessor.Get<Write<CColor>>();
				auto titleComp = accessor.Get<Read<CTitleSprite>>();
				for (auto i = 0; i < entityCount; ++i)
				{
					auto tc = titleComp.value()[i];
					if (tc.isErased) {
						colorComp.value()[i].color.a = 0.0f;
						continue;
					}

					float lt = Math::clamp(fadeElapsedTime / tc.fadeTime, 0.0f, 1.0f);
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

				//Mainのレベルにプレイヤーのシステムを追加
				auto addSystemCmd = worldRequestService->CreateAddSystemCommand<PlayerSystem>(App::MAIN_LEVEL_NAME);
				worldRequestService->PushCommand(std::move(addSystemCmd));
			}

			return;
		}

		// Enterキーが押されたら、ゲームレベルをロードするリクエストを追加
		if (isLoadedMainLevel && inputService->IsKeyTrigger(Input::Key::Enter) && !startGame)
		{
			startGame = true;
			fadeElapsedTime = 0.0f;
		}
	}
private:
	bool startGame = { false };
	std::atomic<bool> loadedMainLevel = { false };
	float fadeElapsedTime = 0.0f;
};
