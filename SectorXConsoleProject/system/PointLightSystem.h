#pragma once

#include <SectorFW/Graphics/PointLightService.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>

struct CPointLight
{
	SFW::Graphics::PointLightHandle handle;
};

template<typename Partition>
class PointLightSystem : public ITypeSystem<
	PointLightSystem<Partition>,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CPointLight>
	>,
	//受け取るサービスの指定
	ServiceContext<
		Graphics::PointLightService,
		Graphics::RenderService,
		Graphics::I3DPerCameraService,
		Graphics::DX11::LightShadowResourceService
	>>{
	using Accessor = ComponentAccessor<Read<CPointLight>>;
public:

	/*void StartImpl(
		UndeletablePtr<Graphics::PointLightService> pointLightService,
		UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::DX11::LightShadowResourceService> resourceService)
	{
		using namespace Graphics;

		std::vector<GpuPointLight>* gpuPointData = new std::vector<GpuPointLight>();
		pointLightService->BuildGpuLights(*gpuPointData);

		auto bufferManager = renderService->GetResourceManager<DX11::BufferManager>();

		auto slot = renderService->GetProduceSlot();

		DX11::BufferUpdateDesc bufferDesc{};
		bufferDesc.buffer = resourceService->GetPointLightBuffer();
		bufferDesc.data = gpuPointData->data();
		bufferDesc.size = sizeof(GpuPointLight) * gpuPointData->size();
		bufferDesc.isDelete = true;
		bufferManager->UpdateBuffer(bufferDesc, slot);
	}*/

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<Graphics::PointLightService> pointLightService,
		UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> perCameraService,
		UndeletablePtr<Graphics::DX11::LightShadowResourceService> resourceService) {

		auto camPos = perCameraService->GetEyePos();
		auto fru = perCameraService->MakeFrustum();
		fru.ClampedFar(camPos, 100.0f);

		auto slot = renderService->GetProduceSlot();

		auto& pointData = this->gpuPointData[slot];

		std::atomic<uint32_t> lightCount{ 0 };

		this->ForEachFrustumNearChunkWithAccessor<IsParallel{ false }>([&](Accessor& accessor, size_t entityCount)
			{
				auto pointLights = accessor.Get<Read<CPointLight>>();

				for(auto i = 0; i < entityCount; ++i)
				{
					auto& light = pointLights.value()[i];
					auto desc = pointLightService->Get(light.handle);

					//フラスタムに当たっていなければスキップ
					if (!fru.IntersectsSphere(desc.positionWS, desc.range)) continue;

					auto index = lightCount.fetch_add(1, std::memory_order_relaxed);

					pointData[index] = Graphics::GpuPointLight{
						desc.positionWS,
						desc.range,
						desc.color,
						desc.intensity,
						desc.castsShadow ? 1u : 0u
					};
				}

			}, partition, fru, camPos);

		auto plCOunt = lightCount.load(std::memory_order_acquire);

		Graphics::DX11::BufferUpdateDesc bufferDesc{};
		bufferDesc.buffer = resourceService->GetPointLightBuffer();
		bufferDesc.data = pointData;
		bufferDesc.size = sizeof(Graphics::GpuPointLight) * plCOunt;
		bufferDesc.isDelete = false;

		auto bufferManager = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
		bufferManager->UpdateBuffer(bufferDesc, slot);

		//更新したライト数をCPU側にも伝える
		auto lightData = resourceService->GetCPULightData(slot);
		lightData.gPointLightCount = plCOunt;
		resourceService->SetCPULightData(slot, lightData);
	}

private:
	Graphics::GpuPointLight gpuPointData[Graphics::RENDER_BUFFER_COUNT][Graphics::PointLightService::MAX_POINT_LIGHT_NUM];
};

