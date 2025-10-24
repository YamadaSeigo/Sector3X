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
	bool temporalSkip = false;
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
		Math::Vec3f camPos = cameraService->GetEyePos();
		auto viewProj = cameraService->GetCameraBufferData().viewProj;
		auto resolution = cameraService->GetResolution();

		const Graphics::Viewport vp = { (int)resolution.x, (int)resolution.y, cameraService->GetFOV() };

		bool drawOcc = false;

		struct KernelParams {
			Graphics::RenderService* renderService;
			Graphics::DX11ModelAssetManager* modelMgr;
			Graphics::DX11MeshManager* meshMgr;
			Graphics::DX11MaterialManager* materialMgr;
			Graphics::DX11PSOManager* psoMgr;
			Graphics::RenderQueue::ProducerSession* queue;
			const Math::Matrix4x4f& viewProj;
			Math::Vec3f cp;
			Graphics::Viewport vp;
			bool drawOcc;
			Math::Matrix3x4f* worldMatrices;
			Math::Matrix4x4f* WVPs;
		};

		KernelParams kp = {
			renderService.get(),
			modelManager,
			meshManager,
			materialManager,
			psoManager,
			&producerSession,
			viewProj,
			camPos,
			vp,
			drawOcc,
			worldMatrices,
			WVPs
		};

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumNearChunkWithAccessor([](Accessor& accessor, size_t entityCount, KernelParams* kp)
			{
				float nearClip = kp->renderService->GetNearClipPlane();

				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Write<CModel>>();

				SoAPositions soaTf = {
					transform->px(), transform->py(), transform->pz(), (uint32_t)entityCount
				};

				//カメラに近い順にソート
				std::vector<uint32_t> order;
				BuildOrder_FixedRadix16(soaTf, kp->cp.x, kp->cp.y, kp->cp.z, nearClip, 1000.0f, order);
				//BuildFrontK_Strict(soaTf, kp->cp.x, kp->cp.y, kp->cp.z, 6, order);

				Math::MTransformSoA mtf = {
					transform->px(), transform->py(), transform->pz(),
					transform->qx(), transform->qy(), transform->qz(), transform->qw(),
					transform->sx(), transform->sy(), transform->sz()
				};

				// ワールド行列を一括生成
				Math::BuildWorldMatrices3x4_FromSoA(mtf, entityCount, kp->worldMatrices); // クォータニオン非正規化

				Math::Mul4x4x3x4_Batch_To4x4_AVX2_RowCombine(kp->viewProj, kp->worldMatrices, kp->WVPs, entityCount);

				for (size_t i = 0; i < entityCount; ++i) {
					uint32_t idx = order[i];
					const auto& worldMtx = kp->worldMatrices[idx];
					const auto& WVP = kp->WVPs[idx];

					auto& modelComp = model.value()[idx];

					auto instanceIdx = kp->queue->AllocInstance({ worldMtx });
					auto& lodBits = modelComp.prevLODBits;

					//モデルアセットを取得
					auto modelAsset = kp->modelMgr->Get(modelComp.handle);
					int subMeshIdx = 0;
					for (const Graphics::DX11ModelAssetData::SubMesh& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (kp->materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (kp->psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						Math::NdcRectWithW ndc;
						float depth = 0.0f;
						//バウンディングスフィアで高速早期判定
						//※WVPがLHのZeroToOne深度範囲を仮定(そうでない場合はZDepthは正確ではない)
						if (!mesh.bs.IsVisible_WVP(WVP, &ndc, &depth)) continue;

						ndc.valid = true;
						int lodCount = (int)mesh.lods.size();

						static constexpr Graphics::LodRefinePolicy lodRefinePolicy = {};

						float areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);

						//小さすぎる場合は無視
						if (areaFrec <= 0.0001f)
						{
							continue;
						}

						//AABBでNDCを再計算するかのステート取得
						auto refineState = Graphics::EvaluateRefineState(
							ndc,
							areaFrec,
							kp->vp.width,
							kp->vp.height,
							depth,
							nearClip,
							Graphics::ExtentsFromAABB(mesh.aabb),
							mesh.lodThresholds,
							lodCount,
							lodRefinePolicy
						);

						modelComp.occluded = false;

						bool refineNeeded = refineState.shouldRefine();
						//AABBで正確にNDCカバー率を計算し直し
						if (refineNeeded)
						{
							modelComp.occluded = true;

							if (mesh.instance.HasData()) [[unlikely]] {
								Graphics::SharedInstanceArena::InstancePool instance = { Math::Mul3x4x4x4_To3x4_SSE(worldMtx, mesh.instance.worldMtx) };
								cmd.instanceIndex = kp->queue->AllocInstance(instance);
								ndc = Math::ProjectAABBToNdc_Fast(WVP * mesh.instance.worldMtx, mesh.aabb);
								areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);
							}
							else {
								cmd.instanceIndex = instanceIdx;
								ndc = Math::ProjectAABBToNdc_Fast(WVP, mesh.aabb);
								areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);
							}
						}
						else
						{
							cmd.instanceIndex = instanceIdx;
						}


						if (ndc.valid)
						{
							//前フレームで映っている場合は1フレームだけカリングをスキップ
							if (modelComp.temporalSkip)
							{
								modelComp.temporalSkip = false;
							}
							else if(refineNeeded)
							{
								if (kp->drawOcc && kp->renderService->IsVisibleInMOC(ndc) != Graphics::MOC::CullingResult::VISIBLE) {
									//modelComp.occluded = true;
									continue;
								}

								modelComp.temporalSkip = true;
							}
							else
							{
								modelComp.temporalSkip = true;
							}
						}
						else {
							modelComp.temporalSkip = false;
							continue;
						}

						int prevLod = (int)lodBits.get(subMeshIdx);
						int ll = Graphics::SelectLodByPixels(areaFrec, mesh.lodThresholds, lodCount, prevLod, kp->vp.width, kp->vp.height, -5.0f);
						if (ll < 0 || ll > 3) [[unlikely]] {
							LOG_ERROR("LOD selection out of range: %d", ll);
							ll = 0;
						}
						lodBits.set(subMeshIdx, (uint8_t)ll);

						if (mesh.occluder.candidate)
						{
							auto prevOccLod = (int)modelComp.prevOCCBits.get(subMeshIdx);
							auto occLod = Graphics::DecideOccluderLOD_FromThresholds(areaFrec, mesh.lodThresholds, prevOccLod, ll, 0.0f);
							modelComp.prevOCCBits.set(subMeshIdx, (uint8_t)occLod);
							if (occLod != Graphics::OccluderLOD::Far)
							{
								size_t occCount = mesh.occluder.meltAABBs.size();
								std::vector<Math::AABB3f> occAABB(occCount);
								for (auto j = 0; j < occCount; ++j)
								{
									occAABB[j] = Math::TransformAABB_Affine(worldMtx, mesh.occluder.meltAABBs[j]);
								}

								std::vector<Graphics::QuadCandidate> outQuad;
								Graphics::SelectOccluderQuads_AVX2(
									occAABB,
									kp->cp,
									kp->viewProj,
									kp->vp,
									occLod,
									outQuad);

								for (const auto& quad : outQuad)
								{
									auto quadMOC = Graphics::ConvertAABBFrontFaceQuadToMoc(quad.quad, kp->viewProj, nearClip);
									if (quadMOC.valid)
									{
										kp->renderService->RenderingOccluderInMOC(quadMOC);
										kp->drawOcc = true;
									}
								}
							}
						}

						//LOG_INFO("Model LOD selected: %d (s=%f)", ll, s);

						auto& meshHandel = mesh.lods[ll].mesh;
						if (kp->meshMgr->IsValid(meshHandel) == false) [[unlikely]] continue;

						cmd.mesh = meshHandel.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, meshHandel.index);
						kp->queue->Push(std::move(cmd));

						subMeshIdx++;
						if (subMeshIdx >= 16) [[unlikely]] {
							LOG_ERROR("Too many subMeshes in model asset");
							return;
						}
					}
				}
			}, partition, fru, camPos, &kp
		);
	}
	private:
		Math::Matrix3x4f worldMatrices[Graphics::MAX_INSTANCES_PER_FRAME] = {};
		Math::Matrix4x4f WVPs[Graphics::MAX_INSTANCES_PER_FRAME] = {};
};
