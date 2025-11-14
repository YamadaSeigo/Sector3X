#pragma once

#include <SectorFW/Graphics/DX11/DX11ModelAssetManager.h>
#include <SectorFW/Debug/UIBus.h>
#include <SectorFW/Math/Rectangle.hpp>

#include "../app/Packed2Bits32.h"

#include "../app/RenderDefine.h"

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

	static constexpr inline uint32_t MAX_OCCLUDER_AABB_NUM  = 64;

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<IThreadExecutor> threadPool, UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> cameraService) {
		//機能を制限したRenderQueueを取得
		auto modelManager = renderService->GetResourceManager<Graphics::DX11::ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11::PSOManager>();

		auto fru = cameraService->MakeFrustum();
		Math::Vec3f camPos = cameraService->GetEyePos();
		auto viewProj = cameraService->GetCameraBufferData().viewProj;
		auto resolution = cameraService->GetResolution();

		const Graphics::Viewport vp = { (int)resolution.x, (int)resolution.y, cameraService->GetFOV() };

		std::vector<Graphics::QuadCandidate> outQuad;
		uint32_t aabbCount = occluderAABBCount.exchange(0, std::memory_order_acquire);
		Graphics::SelectOccluderQuads_AVX2(
			occluderAABBs,
			aabbCount,
			camPos,
			viewProj,
			vp,
			Graphics::OccluderLOD::Near,
			outQuad);

		auto nearClip = renderService->GetNearClipPlane();

		bool drawOcc = false;
		for (const auto& quad : outQuad)
		{
			auto quadMOC = Graphics::ConvertAABBFrontFaceQuadToMoc(quad.quad, viewProj, nearClip);
			if (quadMOC.valid)
			{
				drawOcc = true;
				renderService->RenderingOccluderInMOC(quadMOC);
			}
		}


		struct KernelParams {
			Graphics::RenderService* renderService;
			Graphics::DX11::ModelAssetManager* modelMgr;
			Graphics::DX11::MeshManager* meshMgr;
			Graphics::DX11::MaterialManager* materialMgr;
			Graphics::DX11::PSOManager* psoMgr;
			const Math::Matrix4x4f& viewProj;
			Math::Vec3f cp;
			Math::Vec3f camRight;
			Math::Vec3f camUp;
			Math::Vec3f camForward;
			std::atomic<uint32_t>& occAABBCount;
			Math::AABB3f* occAABBs;
			Graphics::Viewport vp;
			bool drawOcc;
		};

		Math::Vec3f camRight;
		Math::Vec3f camUp;
		Math::Vec3f camForward;
		cameraService->MakeBasis(camRight, camUp, camForward);

		KernelParams kp = {
			renderService.get(),
			modelManager,
			meshManager,
			materialManager,
			psoManager,
			viewProj,
			camPos,
			camRight,
			camUp,
			camForward,
			occluderAABBCount,
			occluderAABBs,
			vp,
			drawOcc
		};

		//アクセスを宣言したコンポーネントにマッチするチャンクに指定した関数を適応する
		this->ForEachFrustumNearChunkWithAccessor<IsParallel{true}>([](Accessor& accessor, size_t entityCount, KernelParams* kp)
			{
				if (entityCount == 0) return;

				//バッファをスレッドごとに確保して渡す
				thread_local Graphics::RenderQueue::ProducerSessionExternal::SmallBuf localQueueBuf;
				auto producer = kp->renderService->GetProducerSession(PassGroupName[GROUP_3D_MAIN], localQueueBuf);

				float nearClip = kp->renderService->GetNearClipPlane();

				//読み取り専用でTransformSoAのアクセサを取得
				auto transform = accessor.Get<Read<TransformSoA>>();
				auto model = accessor.Get<Write<CModel>>();

				SoAPositions soaTf = {
					transform->px(), transform->py(), transform->pz(), (uint32_t)entityCount
				};

				//カメラに近い順にソート
				//std::vector<uint32_t> order;
				//BuildOrder_FixedRadix16(soaTf, kp->cp.x, kp->cp.y, kp->cp.z, nearClip, 1000.0f, order);
				//BuildFrontK_Strict(soaTf, kp->cp.x, kp->cp.y, kp->cp.z, 6, order);

				Math::MTransformSoA mtf = {
					transform->px(), transform->py(), transform->pz(),
					transform->qx(), transform->qy(), transform->qz(), transform->qw(),
					transform->sx(), transform->sy(), transform->sz()
				};

				// ワールド行列を一括生成
				std::vector<float> worldMtxBuffer(12 * entityCount);
				Math::Matrix3x4fSoA worldMtxSoA(worldMtxBuffer.data(), entityCount);
				Math::BuildWorldMatrixSoA_FromTransformSoA(mtf, worldMtxSoA);

				std::vector<Math::Matrix4x4f> WVPs(entityCount);
				Math::Mul4x4x3x4_Batch_To4x4_AVX2_RowCombine_SoA(kp->viewProj, worldMtxSoA, WVPs.data());

				std::vector<Graphics::InstanceIndex> instanceIndices(entityCount);
				producer.AllocInstancesFromWorldSoA(worldMtxSoA, instanceIndices.data());

				for (size_t i = 0; i < entityCount; ++i) {
					const auto& WVP = WVPs[i];

					auto& modelComp = model.value()[i];

					auto instanceIdx = instanceIndices[i];
					auto& lodBits = modelComp.prevLODBits;

					//モデルアセットを取得
					auto modelAsset = kp->modelMgr->Get(modelComp.handle);
					int subMeshIdx = 0;
					for (const Graphics::DX11::ModelAssetData::SubMesh& mesh : modelAsset.ref().subMeshes) {
						Graphics::DrawCommand cmd;
						if (kp->materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (kp->psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						modelComp.occluded = false;

						Math::NdcRectWithW ndc;
						float depth = 0.0f;
						//バウンディングスフィアで高速早期判定
						//※WVPがLHのZeroToOne深度範囲を仮定(そうでない場合はZDepthは正確ではない)
						if (!mesh.bs.IsVisible_WVP_CamBasis_Fast(WVP, kp->camRight, kp->camUp, kp->camForward, &ndc, &depth)) continue;

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

						bool refineNeeded = refineState.shouldRefine();
						//AABBで正確にNDCカバー率を計算し直し
						if (refineNeeded)
						{
							if (mesh.instance.HasData()) [[unlikely]] {
								LOG_INFO("model has instance matrix!");
								Graphics::SharedInstanceArena::InstancePool instance = { Math::Mul3x4x4x4_To3x4_SSE(worldMtxSoA.AoS(i), mesh.instance.worldMtx)};
								cmd.instanceIndex = producer.AllocInstance(instance);
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


						bool temporalSkip = modelComp.temporalSkip;
						modelComp.temporalSkip = false;

						if (!ndc.valid)
						{
							continue;
						}

						//前フレームで映っている場合は1フレームだけカリングをスキップ
						//大きすぎる場合は確実に可視
						if (!temporalSkip)
						{
							if (areaFrec < 0.5f && kp->renderService->IsVisibleInMOC(ndc) != Graphics::MOC::CullingResult::VISIBLE) {
								modelComp.temporalSkip = false;
								modelComp.occluded = true;
								continue;
							}

							modelComp.temporalSkip = true;
						}

						int prevLod = (int)lodBits.get(subMeshIdx);
						float s_occ;
						int ll = Graphics::SelectLodByPixels(areaFrec, mesh.lodThresholds, lodCount, prevLod, kp->vp.width, kp->vp.height, -5.0f, &s_occ);
						if (ll < 0 || ll > 3) [[unlikely]] {
							LOG_ERROR("LOD が範囲外です: %d", ll);
							ll = 0;
						}
						lodBits.set(subMeshIdx, (uint8_t)ll);

						if (mesh.occluder.candidate)
						{
							auto prevOccLod = (int)modelComp.prevOCCBits.get(subMeshIdx);
							auto occLod = Graphics::DecideOccluderLOD_FromThresholds(s_occ, mesh.lodThresholds, prevOccLod, ll, 0.0f);
							modelComp.prevOCCBits.set(subMeshIdx, (uint8_t)occLod);
							if (occLod != Graphics::OccluderLOD::Far)
							{
								size_t occCount = mesh.occluder.meltAABBs.size();
								std::vector<Math::AABB3f> occAABB(occCount);
								Math::Matrix3x4f worldMtx = worldMtxSoA.AoS(i);
								for (auto j = 0; j < occCount; ++j)
								{
									occAABB[j] = Math::TransformAABB_Affine(worldMtx, mesh.occluder.meltAABBs[j]);
								}
								uint32_t base = kp->occAABBCount.fetch_add((uint32_t)occCount, std::memory_order_acq_rel);
								if (base + occCount < MAX_OCCLUDER_AABB_NUM)
								{
									memcpy(&kp->occAABBs[base], occAABB.data(), sizeof(Math::AABB3f) * occCount);
								}
								else
								{
									LOG_WARNING("Occluder AABB buffer overflow");
								}

							}
						}

						auto& meshHandel = mesh.lods[ll].mesh;
						if (kp->meshMgr->IsValid(meshHandel) == false) [[unlikely]] continue;

						cmd.mesh = meshHandel.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;

						cmd.viewMask |= PASS_3DMAIN_ZPREPASS | PASS_3DMAIN_OPAQUE;

						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, meshHandel.index);
						producer.Push(std::move(cmd));

						subMeshIdx++;
						if (subMeshIdx >= 16) [[unlikely]] {
							LOG_ERROR("モデルのサブメッシュ数が多すぎます {%d}", modelAsset.ref().subMeshes.size());
							return;
						}
					}
				}
			}, partition, fru, camPos, &kp, threadPool.get()
		);
	}

private:
	Math::AABB3f occluderAABBs[MAX_OCCLUDER_AABB_NUM];
	std::atomic<uint32_t> occluderAABBCount{ 0 };
};
