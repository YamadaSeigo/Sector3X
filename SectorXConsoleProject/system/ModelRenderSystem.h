#pragma once

#include <SectorFW/Graphics/DX11/DX11ModelAssetManager.h>
#include <SectorFW/Debug/UIBus.h>
#include <SectorFW/Math/Rectangle.hpp>

#include "../app/Packed2Bits32.h"

struct CModel
{
	Graphics::ModelAssetHandle handle;
	Packed2Bits32 prevLODBits = {};
};

template<typename Partition>
class ModelRenderSystem : public ITypeSystem<
	ModelRenderSystem<Partition>,
	Partition,
	ComponentAccess<Read<TransformSoA>, Write<CModel>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService, Graphics::I3DPerCameraService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<TransformSoA>, Write<CModel>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService) {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("Default");
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();

		auto fru = cameraService->MakeFrustum();
		Math::Vec3f camPos = cameraService->GetPosition();
		auto viewProj = cameraService->GetCameraBufferData().viewProj;
		auto resolution = cameraService->GetResolution();

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumNearChunkWithAccessor([](Accessor& accessor, size_t entityCount,
			auto modelMgr, auto meshMgr, auto materialMgr, auto psoMgr,
			auto queue, auto* viewProj, Math::Vec2f resolution)
			{
				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Write<CModel>>();
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

					auto instanceIdx = queue->AllocInstance({ worldMtx });
					auto& lodBits = model.value()[i].prevLODBits;

					int subMeshIdx = 0;
					for (const Graphics::DX11ModelAssetData::SubMesh& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						Math::Rectangle rect;
						if (mesh.instance.HasData()) [[unlikely]] {
							Graphics::InstanceData instance = { worldMtx * mesh.instance.worldMtx };
							cmd.instanceIndex = queue->AllocInstance(instance);
							auto localMVP = (*viewProj) * (worldMtx * mesh.instance.worldMtx);
							rect = Math::ProjectAABBToScreenRect((worldMtx * mesh.instance.worldMtx) * mesh.aabb, *viewProj, resolution.x, resolution.y, -resolution.x * 0.5f, -resolution.y * 0.5f);
						}
						else [[likely]] {
							cmd.instanceIndex = instanceIdx;
							rect = Math::ProjectAABBToScreenRect(worldMtx * mesh.aabb, *viewProj, resolution.x, resolution.y, -resolution.x * 0.5f, -resolution.y * 0.5f);
						}

						float s = (std::min)((rect.width() * rect.height()) / (resolution.x * resolution.y), 1.0f);
						int ll = Graphics::DX11ModelAssetManager::SelectLod(s, mesh.lodThresholds, (int)mesh.lods.size(), (int)lodBits.get(subMeshIdx), -2.0f);
						if (ll < 0 || ll > 3) {
							LOG_ERROR("LOD selection out of range: %d", ll);
							ll = 0;
						}
						lodBits.set(subMeshIdx, (uint8_t)ll);

						//LOG_INFO("Model LOD selected: %d (s=%f)", ll, s);

						auto& meshHandel = mesh.lods[ll].mesh;
						if (meshMgr->IsValid(meshHandel) == false) [[unlikely]] continue;

						cmd.mesh = meshHandel.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, meshHandel.index);
						queue->Push(std::move(cmd));

						subMeshIdx++;
						if (subMeshIdx >= 16) [[unlikely]] {
							LOG_ERROR("Too many subMeshes in model asset");
							return;
						}
					}
				}
			}, partition, fru, camPos, modelManager, meshManager, materialManager, psoManager, &producerSession, &viewProj, resolution
		);
	}
};
