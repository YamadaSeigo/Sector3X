#pragma once

#include "ModelRenderSystem.h"

template<typename Partition>
class CleanModelSystem : public ITypeSystem<
	CleanModelSystem,
	Partition,
	ComponentAccess<Write<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Write<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void EndImpl(Partition& partition, NoDeletePtr<Graphics::RenderService> renderService) {
		Graphics::DX11::ModelAssetManager* modelMgr = renderService->GetResourceManager<Graphics::DX11::ModelAssetManager>();

		this->ForEachChunkWithAccessor([](Accessor& accessor, auto entityCount, Graphics::DX11::ModelAssetManager* modelMgr)
			{
				auto pModel = accessor.Get<Write<CModel>>();
				if (!pModel) return;

				uint64_t deleteFrame = renderService->GetProduceSlot() + Graphics::RENDER_BUFFER_COUNT;
				for (auto i = 0; i < entityCount; ++i)
				{
					auto& model = pModel.value()[i];
					modelMgr->Release(model.handle, deleteFrame);
				}
			}, partition, modelMgr);
	}
};
