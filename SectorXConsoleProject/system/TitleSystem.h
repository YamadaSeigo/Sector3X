#pragma once

template<typename Partition>
class TitleSystem : public ITypeSystem<
	TitleSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<>,
	//受け取るサービスの指定
	ServiceContext<
		WorldType::RequestService,
		InputService
	>>{
	using Accessor = ComponentAccessor<>;

public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(NoDeletePtr<WorldType::RequestService> worldRequestService,
		NoDeletePtr< InputService> inputService) {

		// Enterキーが押されたら、ゲームレベルをロードするリクエストを追加
		if (inputService->IsKeyTrigger(Input::Key::Enter)) {

			auto loadLevelCmd = worldRequestService->CreateLoadLevelCommand("Loading");
			worldRequestService->PushCommand(std::move(loadLevelCmd));

			//ロード完了後のコールバック
			auto loadedFunc = [&](WorldType::Session* pSession) {

				//ローディングレベルをクリーンアップ
				pSession->CleanLevel("Loading");
				};

			//ゲームレベルをロードするリクエストを追加

			auto loadGameLevelCmd = worldRequestService->CreateLoadLevelCommand("OpenField", true, true, loadedFunc);
			worldRequestService->PushCommand(std::move(loadGameLevelCmd));
		}

	}
};
