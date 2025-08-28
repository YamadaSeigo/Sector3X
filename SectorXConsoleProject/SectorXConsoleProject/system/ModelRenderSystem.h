#pragma once

struct CModel
{
	Graphics::ModelAssetHandle handle;
};

template<typename Partition>
class ModelRenderSystem : public ITypeSystem<
	Partition,
	ComponentAccess<Read<TransformSoA>, Read<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<TransformSoA>, Read<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService) override {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("Default");
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount,
			auto modelMgr,auto meshMgr, auto materialMgr, auto psoMgr,
			auto queue)
			{
				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Read<CModel>>();
				for (size_t i = 0; i < entityCount; ++i) {
					Math::Vec3f pos(transform->px()[i], transform->py()[i], transform->pz()[i]);
					Math::Quatf rot(transform->qx()[i], transform->qy()[i], transform->qz()[i], transform->qw()[i]);
					Math::Vec3f scale(transform->sx()[i], transform->sy()[i], transform->sz()[i]);
					auto transMtx = Math::MakeTranslationMatrix(pos);
					auto rotMtx = Math::MakeRotationMatrix(rot);
					auto scaleMtx = Math::MakeScalingMatrix(scale);
					//ワールド行列を計算
					auto worldMtx = transMtx * rotMtx * scaleMtx;

					//モデルアセットを取得
					static Graphics::DX11ModelAssetData modelAsset;
					modelAsset = modelMgr->Get(model.value()[i].handle);

					auto modelIdx = queue->AllocInstance({ worldMtx });

					for (const auto& mesh : modelAsset.subMeshes) {
						Graphics::DrawCommand cmd;
						if (meshMgr->IsValid(mesh.mesh) == false) continue;
						if (materialMgr->IsValid(mesh.material) == false) continue;
						if (psoMgr->IsValid(mesh.pso) == false) continue;

						if (mesh.hasInstanceData) {
							Graphics::InstanceData instance = { worldMtx * mesh.instance.worldMtx };
							cmd.instanceIndex = queue->AllocInstance(instance);
						}
						else {
							cmd.instanceIndex = modelIdx;
						}

						cmd.mesh = mesh.mesh.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, mesh.mesh.index);
						queue->Push(std::move(cmd));
					}
				}
			}, partition, modelManager, meshManager, materialManager, psoManager, &producerSession);
	}
};
