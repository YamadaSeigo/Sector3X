#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>

template<typename Partition>
class ShapeDimsRenderSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService) override {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("DrawLine");
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();

		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount, auto meshMgr, auto queue)
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
						cmd.pso = 0;
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
						cmd.pso = 0;
						cmd.sortKey = 0; // 適切なソートキーを設定
						queue->Push(std::move(cmd));
						break;
					}
					default:
						break;
					}
				}
			}, partition, meshManager, &producerSession);
	}
};
