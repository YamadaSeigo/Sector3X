#pragma once

#include "../app/DefferedRenderingService.h"

template<typename Partition>
class DefferedRenderingSystem : public ITypeSystem<
	DefferedRenderingSystem<Partition>,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<
		Graphics::RenderService
	>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<>;
public:

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<DefferedRenderingService> defferedService,
		UndeletablePtr<Graphics::I3DPerCameraService> perCameraService)
	{
		using namespace Graphics;

		LightCameraBuffer lightCameraBufferData{};
		lightCameraBufferData.invViewProj = Math::Inverse(perCameraService->GetCameraBufferData().viewProj);
		lightCameraBufferData.camForward = perCameraService->GetForward();

		defferedService->UpdateBufferData(std::move(lightCameraBufferData));
	}
};

