#pragma once

#include <SectorFW/Graphics/PointLightService.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>

struct CPointLight
{
	SFW::Graphics::PointLightHandle handle;
};

template<typename Partition>
class PointLightSystem : public ITypeSystem<
	PointLightSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CPointLight>
	>,
	//受け取るサービスの指定
	ServiceContext<
		Graphics::PointLightService,
		Graphics::I3DPerCameraService
	>>{
	using Accessor = ComponentAccessor<Read<CPointLight>>;
public:

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<Graphics::PointLightService> pointLightService,
		NoDeletePtr<Graphics::I3DPerCameraService> perCameraService) {

		auto camPos = perCameraService->GetEyePos();
		auto fru = perCameraService->MakeFrustum();
		fru = fru.ClampedFar(camPos, 100.0f);

		this->ForEachFrustumNearChunkWithAccessor<IsParallel{ false }>([&](Accessor& accessor, size_t entityCount)
			{
				auto pointLights = accessor.Get<Read<CPointLight>>();

				auto readLock = pointLightService->AcquireReadLock();

				for(auto i = 0; i < entityCount; ++i)
				{
					auto& light = pointLights.value()[i];
					auto desc = pointLightService->GetNoLock(light.handle);

					//フラスタムに当たっていなければスキップ
					if (!fru.IntersectsSphere(desc.positionWS, desc.range)) continue;

					pointLightService->PushShowHandle(light.handle);
				}

			}, partition, fru, camPos);
	}
};

