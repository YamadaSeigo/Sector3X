#pragma once

#include <SectorFW/Debug/UIBus.h>

struct CModel
{
	Graphics::ModelAssetHandle handle;
};

template<typename Partition>
class ModelRenderSystem : public ITypeSystem<
	ModelRenderSystem<Partition>,
	Partition,
	ComponentAccess<Read<TransformSoA>, Read<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService, Graphics::I3DCameraService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<TransformSoA>, Read<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DCameraService> cameraService) {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("Default");
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();

		auto fru = cameraService->MakeFrustum();

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumChunkWithAccessor([](Accessor& accessor, size_t entityCount,
			auto modelMgr, auto meshMgr, auto materialMgr, auto psoMgr,
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
					auto modelAsset = modelMgr->Get(model.value()[i].handle);

					auto modelIdx = queue->AllocInstance({ worldMtx });

					for (const auto& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (meshMgr->IsValid(mesh.mesh) == false) [[unlikely]] continue;
						if (materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						if (mesh.hasInstanceData) [[unlikely]] {
							Graphics::InstanceData instance = { worldMtx * mesh.instance.worldMtx };
							cmd.instanceIndex = queue->AllocInstance(instance);
						}
						else [[likely]] {
							cmd.instanceIndex = modelIdx;
						}

						cmd.mesh = mesh.mesh.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, mesh.mesh.index);
						queue->Push(std::move(cmd));
					}
				}

				// ====== SoA で一括エンキューするバッチ実装 ======
				//static thread_local std::vector<Graphics::InstanceData> instBuf;
				//static thread_local std::vector<uint32_t> meshBuf, matBuf, psoBuf, instIxBuf;
				//static thread_local std::vector<uint64_t> sortBuf;

				//instBuf.clear(); meshBuf.clear(); matBuf.clear(); psoBuf.clear(); instIxBuf.clear(); sortBuf.clear();
				//instBuf.reserve(entityCount);

				//// まずは全エンティティ分の InstanceData を作成（モデル共通）
				//for (size_t i = 0; i < entityCount; ++i) {
				//	Math::Vec3f pos(transform->px()[i], transform->py()[i], transform->pz()[i]);
				//	Math::Quatf rot(transform->qx()[i], transform->qy()[i], transform->qz()[i], transform->qw()[i]);
				//	Math::Vec3f scale(transform->sx()[i], transform->sy()[i], transform->sz()[i]);
				//	auto transMtx = Math::MakeTranslationMatrix(pos);
				//	auto rotMtx = Math::MakeRotationMatrix(rot);
				//	auto scaleMtx = Math::MakeScalingMatrix(scale);
				//	auto worldMtx = transMtx * rotMtx * scaleMtx; // ワールド行列
				//	instBuf.push_back(Graphics::InstanceData{ worldMtx });
				//}

				//// InstanceData をまとめて確保・コピー
				//auto bulk = queue->AllocInstancesBulk(instBuf.data(), static_cast<uint32_t>(instBuf.size()));
				//const uint32_t baseInst = bulk.first.index;

				//// 各エンティティのモデルのサブメッシュを SoA に積む
				//for (size_t i = 0; i < entityCount; ++i) {
				//	auto modelAsset = modelMgr->Get(model.value()[i].handle);
				//	const uint32_t modelInstIx = baseInst + static_cast<uint32_t>(i);
				//	for (const auto& mesh : modelAsset.ref().subMeshes) {
				//		if (!meshMgr->IsValid(mesh.mesh))      continue;
				//		if (!materialMgr->IsValid(mesh.material)) continue;
				//		if (!psoMgr->IsValid(mesh.pso))           continue;

				//		uint32_t instIx = modelInstIx;
				//		// サブメッシュ固有のインスタンスデータがある場合は置き換え
				//		if (mesh.hasInstanceData) {
				//			Graphics::InstanceData inst = { instBuf[i].worldMtx * mesh.instance.worldMtx };
				//			instIx = queue->AllocInstance(inst).index;

				//		}

				//		meshBuf.push_back(mesh.mesh.index);
				//		matBuf.push_back(mesh.material.index);
				//		psoBuf.push_back(mesh.pso.index);
				//		instIxBuf.push_back(instIx);
				//		sortBuf.push_back(Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, mesh.mesh.index));

				//	}

				//}

				//// SoA をまとめて一括投入
				//queue->PushSOA({
				//meshBuf.data(),
				//matBuf.data(),
				//psoBuf.data(),
				//instIxBuf.data(),
				//sortBuf.data(),
				//meshBuf.size()
				//	});

			}, partition, fru, modelManager, meshManager, materialManager, psoManager, &producerSession
		);
	}
};
