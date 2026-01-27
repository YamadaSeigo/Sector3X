#pragma once

#include <SectorFW/Graphics/PointLightService.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>

template<typename Partition>
class LightShadowSystem : public ITypeSystem<
	LightShadowSystem,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<
		InputService,
		Graphics::I3DPerCameraService,
		Graphics::RenderService,
		Graphics::LightShadowService,
		Graphics::PointLightService,
		Graphics::DX11::LightShadowResourceService
	>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<>;
public:
	void StartImpl(
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<Graphics::I3DPerCameraService> perCameraService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::LightShadowService> lightShadowService,
		NoDeletePtr<Graphics::PointLightService> pointLightService,
		NoDeletePtr<Graphics::DX11::LightShadowResourceService> shadowMapService
	)
	{
		Graphics::CameraParams camParams;
		camParams.view = perCameraService->MakeViewMatrix();
		camParams.position = perCameraService->GetEyePos();
		camParams.nearPlane = perCameraService->GetNearClip();
		camParams.farPlane = perCameraService->GetFarClip();
		camParams.fovY = perCameraService->GetFOV();
		camParams.aspect = perCameraService->GetAspectRatio();

		lightShadowService->UpdateCascade(camParams, cascadeSceneAABB);

		float ambientIntensity = lightShadowService->GetAmbientLight().intensity;

		REGISTER_DEBUG_SLIDER_FLOAT("Light", "AmbientIntensity", ambientIntensity, 0.0f, 10.0f, 0.05f, [=](float value) {
			Graphics::AmbientLight ambient = lightShadowService->GetAmbientLight();
			ambient.intensity = value;

			lightShadowService->SetAmbientLight(ambient);
			});

		float emissiveBoost = lightShadowService->GetEmissiveBoost();

		REGISTER_DEBUG_SLIDER_FLOAT("Light", "EmissiveBoost", emissiveBoost, 0.0f, 10.0f, 0.01f, [=](float value) {
			lightShadowService->SetEmissiveBoost(value);
			});
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(
		NoDeletePtr<InputService> inputService,
		NoDeletePtr<Graphics::I3DPerCameraService> perCameraService,
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::LightShadowService> lightShadowService,
		NoDeletePtr<Graphics::PointLightService> pointLightService,
		NoDeletePtr<Graphics::DX11::LightShadowResourceService> resourceService) {

		bool updateCascade = false;
		if (inputService->IsKeyPressed(Input::Key::L))
		{
			const float lightRotateSpeed = Math::Deg2Rad(1.0f);

			auto dirLight = lightShadowService->GetDirectionalLight();

			if (inputService->IsKeyPressed(Input::Key::Left))
			{
				updateCascade = true;

				auto dir = dirLight.directionWS;

				dirLight.directionWS = {
					cos(lightRotateSpeed) * dir.x - sin(lightRotateSpeed) * dir.z,
					dirLight.directionWS.y,
					sin(lightRotateSpeed) * dir.x + cos(lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Right))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					cos(-lightRotateSpeed) * dir.x - sin(-lightRotateSpeed) * dir.z,
					dirLight.directionWS.y,
					sin(-lightRotateSpeed) * dir.x + cos(-lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Up))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					dirLight.directionWS.x,
					cos(lightRotateSpeed) * dir.y - sin(lightRotateSpeed) * dir.z,
					sin(lightRotateSpeed) * dir.y + cos(lightRotateSpeed) * dir.z,
				};
			}
			if (inputService->IsKeyPressed(Input::Key::Down))
			{
				updateCascade = true;
				auto dir = dirLight.directionWS;
				dirLight.directionWS = {
					dirLight.directionWS.x,
					cos(-lightRotateSpeed) * dir.y - sin(-lightRotateSpeed) * dir.z,
					sin(-lightRotateSpeed) * dir.y + cos(-lightRotateSpeed) * dir.z,
				};
			}

			lightShadowService->SetDirectionalLight(dirLight);
		}

		//カスケードとライトデータの更新
		{
			Graphics::CameraParams camParams;
			camParams.view = perCameraService->MakeViewMatrix();
			camParams.position = perCameraService->GetEyePos();
			camParams.nearPlane = perCameraService->GetNearClip();
			camParams.farPlane = perCameraService->GetFarClip();
			camParams.fovY = perCameraService->GetFOV();
			camParams.aspect = perCameraService->GetAspectRatio();

			lightShadowService->UpdateCascade(camParams, cascadeSceneAABB);

			auto currentSlot = renderService->GetProduceSlot();

			auto& dst = m_cbShadowCascadesData[currentSlot];

			const auto& cascade = lightShadowService->GetCascades();
			using CascadeType = std::remove_const_t<std::remove_reference_t<decltype(cascade)>>;

			std::memcpy(dst.lightViewProj, cascade.lightViewProj.data(), sizeof(dst.lightViewProj));
			std::memcpy(dst.splitDepths, lightShadowService->GetSplitDistances().data(), sizeof(float) * CascadeType::kNumCascades);
			std::memcpy(dst.dir, &lightShadowService->GetDirectionalLight().directionWS, sizeof(float) * 3);

			Graphics::DX11::BufferUpdateDesc cbUpdateDesc{};
			cbUpdateDesc.buffer = resourceService->GetShadowCascadesBuffer();
			cbUpdateDesc.data = &dst;
			cbUpdateDesc.size = sizeof(Graphics::DX11::CBShadowCascadesData);
			cbUpdateDesc.isDelete = false;

			auto bufferMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
			bufferMgr->UpdateBuffer(cbUpdateDesc, currentSlot);

			auto& cpuLightData = m_cpuLightData[currentSlot];
			cpuLightData = lightShadowService->GetCPULightData();

			const auto* pointLightData = pointLightService->BuildGpuLights(cpuLightData.gPointLightCount);

			// CPU 側のライトデータを GPU 側に転送
			cbUpdateDesc.buffer = resourceService->GetLightDataCB();
			cbUpdateDesc.data = &cpuLightData;
			cbUpdateDesc.size = sizeof(Graphics::CPULightData);
			bufferMgr->UpdateBuffer(cbUpdateDesc, currentSlot);

			cbUpdateDesc.buffer = resourceService->GetPointLightBuffer();
			cbUpdateDesc.data = pointLightData;
			cbUpdateDesc.size = sizeof(Graphics::GpuPointLight) * cpuLightData.gPointLightCount;
			bufferMgr->UpdateBuffer(cbUpdateDesc, currentSlot);
		}
	}
private:
	Math::AABB3f cascadeSceneAABB = { {0,-500.0f,0},{5000.0f,500.0f,5000.0f} };
	Graphics::DX11::CBShadowCascadesData m_cbShadowCascadesData[Graphics::RENDER_BUFFER_COUNT] = {};
	Graphics::CPULightData m_cpuLightData[::SFW::Graphics::RENDER_BUFFER_COUNT] = {};
};