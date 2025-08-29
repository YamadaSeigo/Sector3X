#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>

template<typename Partition>
class ShapeDimsRenderSystem : public ITypeSystem<
	ShapeDimsRenderSystem<Partition>,
	Partition,
	ComponentAccess<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>;
public:
	void StartImpl(UndeletablePtr<Graphics::RenderService> renderService) {
		using namespace Graphics;

		auto shaderMgr = renderService->GetResourceManager<DX11ShaderManager>();
		DX11ShaderCreateDesc shaderDesc;
		shaderDesc.templateID = MaterialTemplateID::PBR;
		shaderDesc.vsPath = L"asset/shader/VS_DrawLineList.cso";
		shaderDesc.psPath = L"asset/shader/PS_DrawLineList.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		auto psoMgr = renderService->GetResourceManager<DX11PSOManager>();
		DX11PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::WireCullNone };
		psoMgr->Add(psoDesc, psoHandle);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService) {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("DrawLine");
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();
		if (!psoManager->IsValid(psoHandle)) {
			LOG_ERROR("PSOHandle is not valid in ShapeDimsRenderSystem");
			return;
		}

		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount, auto meshMgr, auto queue, auto pso)
			{
				auto shapeDims = accessor.Get<Read<Physics::ShapeDims>>();
				auto interp = accessor.Get<Read<Physics::PhysicsInterpolation>>();

				if (!shapeDims) return;
				if (!interp) return;

				for (int i = 0; i < entityCount; ++i) {
					auto transMtx = Math::MakeTranslationMatrix(Math::Vec3f(interp->cpx()[i], interp->cpy()[i], interp->cpz()[i]));
					auto rotMtx = Math::MakeRotationMatrix(Math::Quatf(interp->crx()[i], interp->cry()[i], interp->crz()[i], interp->crw()[i]));

					auto& d = shapeDims.value()[i];
					switch (d.type) {
					case Physics::ShapeDims::Type::Box: {
						auto mtx = transMtx * rotMtx * Math::MakeScalingMatrix(d.dims);
						Graphics::DrawCommand cmd;
						cmd.instanceIndex = queue->AllocInstance({ mtx });
						cmd.mesh = meshMgr->GetBoxMesh().index;
						cmd.material = 0;
						cmd.pso = pso;
						cmd.sortKey = 0; // 適切なソートキーを設定
						queue->Push(std::move(cmd));
						break;
					}
					case Physics::ShapeDims::Type::Sphere: {
						auto mtx = transMtx * rotMtx * Math::MakeScalingMatrix(Math::Vec3f(d.r * 2)); // 球は均一スケーリング
						Graphics::DrawCommand cmd;
						cmd.instanceIndex = queue->AllocInstance({ mtx });
						cmd.mesh = meshMgr->GetSphereMesh().index;
						cmd.material = 0;
						cmd.pso = pso;
						cmd.sortKey = 0; // 適切なソートキーを設定
						queue->Push(std::move(cmd));
						break;
					}
					default:
						break;
					}
				}
			}, partition, meshManager, &producerSession, psoHandle.index);
	}
private:
	Graphics::PSOHandle psoHandle = {};
};
