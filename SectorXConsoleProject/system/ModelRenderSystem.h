#pragma once

#include <SectorFW/Graphics/DX11/DX11ModelAssetManager.h>
#include <SectorFW/Debug/UIBus.h>
#include <SectorFW/Math/Rectangle.hpp>

#include "../app/Packed2Bits32.h"

struct CModel
{
	Graphics::ModelAssetHandle handle;
	Packed2Bits32 prevLODBits = {};
	Packed2Bits32 prevOCCBits = {};
	bool occluded = false;
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
		auto producerSession = renderService->GetProducerSession("3D");
		auto modelManager = renderService->GetResourceManager<Graphics::DX11ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();

		auto fru = cameraService->MakeFrustum();
		Math::Vec3f camPos = cameraService->GetPosition();
		auto viewProj = cameraService->GetCameraBufferData().viewProj;
		auto resolution = cameraService->GetResolution();

		const Graphics::Viewport vp = { (int)resolution.x, (int)resolution.y, cameraService->GetFOV() };

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumNearChunkWithAccessor([](Accessor& accessor, size_t entityCount,
			Graphics::RenderService* renderService, auto modelMgr, auto meshMgr, auto materialMgr, auto psoMgr,
			auto queue, auto* viewProj, Math::Vec3f cp, const Graphics::Viewport* vp)
			{
				float nearClip = renderService->GetNearClipPlane();

				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Write<CModel>>();

				SoATransforms soaTf = {
					transform->px(), transform->py(), transform->pz(), (uint32_t)entityCount
				};

				std::vector<uint32_t> order;
				BuildOrder_FixedRadix16(soaTf, cp.x, cp.y, cp.z, nearClip, 1000.0f, order);
				//BuildFrontK_Strict(soaTf, cp.x, cp.y, cp.z, 6, order);

				for (size_t i = 0; i < entityCount; ++i) {
					uint32_t idx = order[i];
					Math::Vec3f pos(transform->px()[idx], transform->py()[idx], transform->pz()[idx]);
					Math::Quatf rot(transform->qx()[idx], transform->qy()[idx], transform->qz()[idx], transform->qw()[idx]);
					Math::Vec3f scale(transform->sx()[idx], transform->sy()[idx], transform->sz()[idx]);
					auto transMtx = Math::MakeTranslationMatrix(pos);
					auto rotMtx = Math::MakeRotationMatrix(rot);
					auto scaleMtx = Math::MakeScalingMatrix(scale);
					//ワールド行列を計算
					auto worldMtx = transMtx * rotMtx * scaleMtx;

					//モデルアセットを取得
					auto modelAsset = modelMgr->Get(model.value()[idx].handle);

					auto instanceIdx = queue->AllocInstance({ worldMtx });
					auto& lodBits = model.value()[idx].prevLODBits;

					int subMeshIdx = 0;
					for (const Graphics::DX11ModelAssetData::SubMesh& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						Math::NdcRectWithW ndc;
						if (mesh.instance.HasData()) [[unlikely]] {
							Graphics::InstanceData instance = { worldMtx * mesh.instance.worldMtx };
							cmd.instanceIndex = queue->AllocInstance(instance);
							auto localMVP = (*viewProj) * (worldMtx * mesh.instance.worldMtx);
							ndc = Math::ProjectAABBToNdcRectWithW_SIMD((worldMtx * mesh.instance.worldMtx) * mesh.aabb, *viewProj);
						}
						else [[likely]] {
							cmd.instanceIndex = instanceIdx;
							ndc = Math::ProjectAABBToNdcRectWithW_SIMD(worldMtx * mesh.aabb, *viewProj);
						}

						if (ndc.valid)
						{
							if (renderService->IsVisibleInMOC(ndc) != Graphics::MOC::CullingResult::VISIBLE) {
								model.value()[idx].occluded = true;
								continue;
							}
						}
						else {
							continue;
						}

						model.value()[idx].occluded = false;

						float s = (std::min)(Graphics::NDCCoverageFromRectPx(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);
						int prevLod = (int)lodBits.get(subMeshIdx);
						int ll = Graphics::DX11ModelAssetManager::SelectLod(s, mesh.lodThresholds, (int)mesh.lods.size(), prevLod, -5.0f);
						if (ll < 0 || ll > 3) [[unlikely]] {
							LOG_ERROR("LOD selection out of range: %d", ll);
							ll = 0;
						}
						lodBits.set(subMeshIdx, (uint8_t)ll);

						if (mesh.occluder.candidate)
						{
							auto prevOccLod = (int)model.value()[idx].prevOCCBits.get(subMeshIdx);
							auto occLod = Graphics::DecideOccluderLOD_FromThresholds(s, mesh.lodThresholds, prevOccLod, ll, -0.5f);
							model.value()[idx].prevOCCBits.set(subMeshIdx, (uint8_t)occLod);
							if (occLod != Graphics::OccluderLOD::Far)
							{
								size_t occCount = mesh.occluder.meltAABBs.size();
								std::vector<Math::AABB3f> occAABB(occCount);
								for (auto j = 0; j < occCount; ++j)
								{
									occAABB[j] = worldMtx * mesh.occluder.meltAABBs[j];
								}

								std::vector<Graphics::QuadCandidate> outQuad;
								Graphics::SelectOccluderQuads_AVX2(
									occAABB,
									cp,
									*viewProj,
									*vp,
									occLod,
									outQuad);

								for (const auto& quad : outQuad)
								{
									auto quadMOC = Graphics::ConvertAABBFrontFaceQuadToMoc(quad.quad, *viewProj, nearClip);
									if (quadMOC.valid)
									{
										renderService->RenderingOccluderInMOC(quadMOC);
									}
								}
							}
						}

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
			}, partition, fru, camPos, renderService.get(), modelManager, meshManager, materialManager,
			psoManager, &producerSession, &viewProj, camPos, &vp
		);
	}
};
