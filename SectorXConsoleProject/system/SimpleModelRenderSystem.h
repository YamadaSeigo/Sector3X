#pragma once

#include <SectorFW/Graphics/DX11/DX11ModelAssetManager.h>
#include <SectorFW/Debug/UIBus.h>
#include <SectorFW/Math/Rectangle.hpp>

#include "../app/Packed2Bits32.h"

#include "ModelRenderSystem.h"

template<typename Partition>
class SimpleModelRenderSystem : public ITypeSystem<
	SimpleModelRenderSystem<Partition>,
	Partition,
	ComponentAccess<Read<TransformSoA>, Write<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService, Graphics::I3DPerCameraService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<TransformSoA>, Write<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService) {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("3D");
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();

		auto fru = cameraService->MakeFrustum();
		Math::Vec3f camPos = cameraService->GetEyePos();

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumChunkWithAccessor([](Accessor& accessor, size_t entityCount,
			Graphics::RenderService* renderService, auto modelMgr, auto meshMgr, auto materialMgr, auto psoMgr,
			auto queue)
			{
				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Write<CModel>>();

				Math::MTransformSoA mtf = {
					transform->px(), transform->py(), transform->pz(),
					transform->qx(), transform->qy(), transform->qz(), transform->qw(),
					transform->sx(), transform->sy(), transform->sz()
				};

				std::vector<Math::Matrix4x4f> worldMatrices(entityCount);
				Math::BuildWorldMatrices_FromSoA(mtf, entityCount, worldMatrices.data(), false); // クォータニオン非正規化

				for (size_t i = 0; i < entityCount; ++i) {
					size_t& idx = i;
					//Math::Vec3f pos(transform->px()[i], transform->py()[i], transform->pz()[i]);
					//Math::Quatf rot(transform->qx()[i], transform->qy()[i], transform->qz()[i], transform->qw()[i]);
					//Math::Vec3f scale(transform->sx()[i], transform->sy()[i], transform->sz()[i]);
					//auto transMtx = Math::MakeTranslationMatrix(pos);
					//auto rotMtx = Math::MakeRotationMatrix(rot);
					//auto scaleMtx = Math::MakeScalingMatrix(scale);
					////ワールド行列を計算
					//auto worldMtx =  transMtx * rotMtx * scaleMtx;
					const auto& worldMtx = worldMatrices[i];

					//モデルアセットを取得
					auto modelAsset = modelMgr->Get(model.value()[idx].handle);

					Graphics::InstanceData instance = { worldMtx };
					auto instanceIdx = queue->AllocInstance(std::move(instance));
					auto& lodBits = model.value()[idx].prevLODBits;

					for (const Graphics::DX11ModelAssetData::SubMesh& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						auto& meshHandel = mesh.lods[0].mesh;
						if (meshMgr->IsValid(meshHandel) == false) [[unlikely]] continue;

						if (mesh.instance.HasData()) [[unlikely]] {
							Graphics::InstanceData instance = { worldMtx * mesh.instance.worldMtx };
							cmd.instanceIndex = queue->AllocInstance(instance);
						}
						else [[likely]] {
							cmd.instanceIndex = instanceIdx;
							}

						cmd.mesh = meshHandel.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, meshHandel.index);
						queue->Push(std::move(cmd));
					}
				}
			}, partition, fru, renderService.get(), modelManager, meshManager, materialManager,
				psoManager, &producerSession
				);
	}
};
