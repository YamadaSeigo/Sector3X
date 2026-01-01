#pragma once

#include "../app/DeferredRenderingService.h"

template<typename Partition>
class DeferredRenderingSystem : public ITypeSystem<
	DeferredRenderingSystem,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<
		DeferredRenderingService,
		Graphics::I3DPerCameraService
	>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<>;
public:

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(
		NoDeletePtr<DeferredRenderingService> deferredService,
		NoDeletePtr<Graphics::I3DPerCameraService> perCameraService)
	{
		using namespace Graphics;

		LightCameraBuffer lightCameraBufferData{};
		lightCameraBufferData.invViewProj = Math::Inverse(perCameraService->GetCameraBufferDataNoLock().viewProj);
		lightCameraBufferData.camForward = perCameraService->GetForward();
		lightCameraBufferData.camPos = perCameraService->GetEyePos();

		deferredService->UpdateBufferData(lightCameraBufferData);
	}
};

