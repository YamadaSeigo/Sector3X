#pragma once

#include <SectorFW/Graphics/DX11/DX11ModelAssetManager.h>
#include <SectorFW/Debug/UIBus.h>
#include <SectorFW/Math/Rectangle.hpp>

#include "../app/Packed2Bits32.h"

#include "../app/RenderDefine.h"

//描画系のこのクラスでいったんバッファの更新
#include "../app/WindMovementService.h"

#ifdef _DEBUG
#define PROFILE_MODEL_UPDATE_TIME 1
#endif

struct CModel
{
	Graphics::ModelAssetHandle handle;
	Packed2Bits32 prevLODBits = {};
	Packed2Bits32 prevOCCBits = {};
	bool occluded = false;
	bool temporalSkip = false;
	bool castShadow = false;
	bool outline = false;
};



template<typename Partition>
class ModelRenderSystem : public ITypeSystem<
	ModelRenderSystem<Partition>,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<TransformSoA>,
		Write<CModel>
	>,
	//受け取るサービスの指定
	ServiceContext<
		Graphics::RenderService,
		Graphics::I3DPerCameraService,
		Graphics::LightShadowService,
		WindMovementService
	>,
	//Updateを並列化する
	IsParallel{ true }
	>
{
	using Accessor = ComponentAccessor<Read<TransformSoA>, Write<CModel>>;
public:

	static constexpr inline uint32_t MAX_OCCLUDER_AABB_NUM  = 64;

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		safe_ptr<IThreadExecutor> threadPool,
		safe_ptr<Graphics::RenderService> renderService,
		safe_ptr<Graphics::I3DPerCameraService> cameraService,
		safe_ptr<Graphics::LightShadowService> lightShadowService,
		safe_ptr<WindMovementService> grassService)
	{
		//草のバッファの更新
		grassService->UpdateBufferToGPU(renderService->GetProduceSlot());

		//機能を制限したRenderQueueを取得
		auto modelManager = renderService->GetResourceManager<Graphics::DX11::ModelAssetManager>();
		auto meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11::PSOManager>();

		Math::Vec3f camPos = cameraService->GetEyePos();
		auto viewProj = cameraService->GetCameraBufferDataNoLock().viewProj;
		auto resolution = cameraService->GetResolution();

		auto fru = cameraService->MakeFrustum(true);
		// far を200mに制限したフラスタムを作成
		fru = fru.ClampedFar(camPos, 200.0f);

		float maxShadowDistance = lightShadowService->GetMaxShadowDistance();
		Math::Vec3f shadowDir = lightShadowService->GetDirectionalLight().directionWS.normalized() * -1.0f;
		auto shadowFru = fru.PushedAlongDirection(shadowDir, maxShadowDistance);

		const Graphics::OccluderViewport vp = { (int)resolution.x, (int)resolution.y, cameraService->GetFOV() };

		std::vector<Graphics::QuadCandidate> outQuad;
		uint32_t aabbCount = occluderAABBCount.exchange(0, std::memory_order_acquire);
		Graphics::SelectOccluderQuads_AVX2(
			occluderAABBs,
			aabbCount,
			camPos,
			viewProj,
			vp,
			Graphics::OccluderLOD::Near,
			outQuad,
			Graphics::AABBQuadAxisBit::X | Graphics::AABBQuadAxisBit::Z);

		auto nearClip = renderService->GetNearClipPlane();

		bool drawOcc = false;
		for (const auto& quad : outQuad)
		{
			auto quadMOC = Graphics::ConvertAABBFrontFaceQuadToMoc(quad.clip, viewProj, nearClip);
			if (quadMOC.valid)
			{
				drawOcc = true;
				renderService->RenderingOccluderInMOC(quadMOC);
			}
		}


		struct KernelParams {
			Graphics::RenderService* renderService;
			Graphics::LightShadowService* lightShadowService;
			Graphics::DX11::ModelAssetManager* modelMgr;
			Graphics::DX11::MeshManager* meshMgr;
			Graphics::DX11::MaterialManager* materialMgr;
			Graphics::DX11::PSOManager* psoMgr;
			const Math::Matrix4x4f& viewProj;
			const Math::Frustumf& shadowFrustum;
			Math::Vec3f cp;
			Math::Vec3f camRight;
			Math::Vec3f camUp;
			Math::Vec3f camForward;
			Math::Vec3f shadowDir;
			float maxShadowDistance;
			std::atomic<uint32_t>& occAABBCount;
			Math::AABB3f* occAABBs;
			Graphics::OccluderViewport vp;
			bool drawOcc;
		};

		Math::Vec3f camRight;
		Math::Vec3f camUp;
		Math::Vec3f camForward;
		cameraService->MakeBasis(camRight, camUp, camForward);

		KernelParams kp = {
			renderService.get(),
			lightShadowService.get(),
			modelManager,
			meshManager,
			materialManager,
			psoManager,
			viewProj,
			shadowFru,
			camPos,
			camRight,
			camUp,
			camForward,
			shadowDir,
			maxShadowDistance,
			occluderAABBCount,
			occluderAABBs,
			vp,
			drawOcc
		};

#if PROFILE_MODEL_UPDATE_TIME
		clock_t start = clock();
#endif

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
				Math::BuildWorldMatrixSoA_FromTransformSoA(mtf, worldMtxSoA, false);

				std::vector<Math::Matrix4x4f> WVPs(entityCount);
				Math::Mul4x4x3x4_Batch_To4x4_AVX2_RowCombine_SoA(kp->viewProj, worldMtxSoA, WVPs.data());

				std::vector<Graphics::InstanceIndex> instanceIndices(entityCount);
				producer.AllocInstancesFromWorldSoA(worldMtxSoA, instanceIndices.data());

				//ループの外で読み取りロックを取得
				auto readLock = kp->modelMgr->AcquireReadLock();

				for (size_t i = 0; i < entityCount; ++i) {
					const auto& WVP = WVPs[i];

					auto& modelComp = model.value()[i];

					auto& lodBits = modelComp.prevLODBits;

					//モデルアセットを取得(ロックなし)
					const auto& modelAsset = kp->modelMgr->GetNoLock(modelComp.handle);
					int subMeshIdx = 0;

					float minAreaFrec = modelAsset.minAreaFrec;

					for (const Graphics::DX11::ModelAssetData::SubMesh& mesh : modelAsset.subMeshes) {
						if (kp->materialMgr->IsValid(mesh.material) == false) [[unlikely]] continue;
						if (kp->psoMgr->IsValid(mesh.pso) == false) [[unlikely]] continue;

						modelComp.occluded = false;

						Math::NdcRectWithW ndc;
						float depth = 0.0f;

						//バウンディングスフィアで高速早期判定
						//※WVPがLHのZeroToOne深度範囲を仮定(そうでない場合はZDepthは正確ではない)
						//カメラの軸にWVPの回転成分を反映させないために、radiusだけworld空間にしておく
						//(スケールの一番大きいものを反映しているため可視判定もすこし通りやすくなる)
						float bsRadiusWS = mesh.bs.radius * (std::max)(mtf.sx[i], (std::max)(mtf.sy[i], mtf.sz[i]));
						bool shadowOnly = false;
						Math::Vec3f centerWS = mesh.bs.center;
						if (!Math::BoundingSpheref::IsVisible_LocalCenter_WorldRadius(WVP, kp->viewProj, mesh.bs.center, bsRadiusWS, kp->camRight, kp->camUp, kp->camForward, &ndc, &depth))
						{
							if (!modelComp.castShadow || depth > 0.2f) continue;

							Math::MulPoint_RowMajor_ColVec(
								worldMtxSoA.AoS(i),
								centerWS.x, centerWS.y, centerWS.z,
								centerWS.x, centerWS.y, centerWS.z
							);

							shadowOnly = kp->shadowFrustum.IntersectsSphere(centerWS, bsRadiusWS);

							if (!shadowOnly) continue;
						}

						//if (!mesh.bs.IsVisible_WVP_CamBasis_Fast(WVP, kp->camRight, kp->camUp, kp->camForward, &ndc, &depth)) continue;

						Graphics::InstanceIndex instanceIdx = instanceIndices[i];

						ndc.valid = true;
						int lodCount = (int)mesh.lods.size();

						static constexpr Graphics::LodRefinePolicy lodRefinePolicy = {};

						float areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);

						if (!shadowOnly)
						{
							//小さすぎる場合は無視
							if (areaFrec <= minAreaFrec)
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
									Graphics::SharedInstanceArena::InstancePool instance = { Math::Mul3x4x4x4_To3x4_SSE(worldMtxSoA.AoS(i), mesh.instance.worldMtx) };
									instanceIdx = producer.AllocInstance(instance);
									ndc = Math::ProjectAABBToNdc_Fast(WVP * mesh.instance.worldMtx, mesh.aabb);
									areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);
								}
								else {
									ndc = Math::ProjectAABBToNdc_Fast(WVP, mesh.aabb);
									areaFrec = (std::min)(Graphics::ComputeNDCAreaFrec(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax), 1.0f);
								}
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
						}

						int prevLod = (int)lodBits.get(subMeshIdx);
						float s_occ;
						int ll = Graphics::SelectLodByPixels(areaFrec, mesh.lodThresholds, lodCount, prevLod, kp->vp.width, kp->vp.height, -5.0f, &s_occ);
						if (ll < 0 || ll > 3) [[unlikely]] {
							LOG_ERROR("LOD が範囲外です: %d", ll);
							ll = 0;
						}

						lodBits.set(subMeshIdx, (uint8_t)ll);

						auto& meshHandel = mesh.lods[ll].mesh;
						if (kp->meshMgr->IsValid(meshHandel) == false) [[unlikely]] continue;

						Graphics::DrawCommand cmd;
						cmd.mesh = meshHandel.index;
						cmd.material = mesh.material.index;
						cmd.pso = mesh.pso.index;
						cmd.instanceIndex = instanceIdx;

						if (!shadowOnly)
						{
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

							cmd.viewMask |= modelComp.outline ? PASS_3DMAIN_OUTLINE : PASS_3DMAIN_OPAQUE;

							if (ll < 2)
							{
								if (modelComp.castShadow)
								{
									Math::MulPoint_RowMajor_ColVec(
										worldMtxSoA.AoS(i),
										centerWS.x, centerWS.y, centerWS.z,
										centerWS.x, centerWS.y, centerWS.z
									);

									auto centerVec = centerWS - kp->cp;
									float camDepth = centerVec.dot(kp->camForward);

									// 影方向とカメラ Forward の cosθ
									float cosLF = std::fabs(kp->shadowDir.dot(kp->camForward)); // 0..1

									// 影としてどこまで考慮するか（ワールド距離）
									// 例：一番遠いカスケードの far クリップ距離 - near 距離
									float maxShadowLenWS = kp->maxShadowDistance;

									// カメラ深度方向に投影される「影の余白」
									// 光がカメラとほぼ平行だと cosLF ≈ 1 になって深く伸びる
									float extraDepth = bsRadiusWS + maxShadowLenWS * cosLF;

									// これでカスケード範囲を決める
									float minD = camDepth - extraDepth;
									float maxD = camDepth + extraDepth;

									auto cascades = kp->lightShadowService->GetCascadeIndexRangeUnlock(minD, maxD);

									for (uint32_t ci = cascades.first; ci <= cascades.second; ++ci)
									{
										cmd.viewMask |= (PASS_3DMAIN_CASCADE0 << ci);
									}
								}
							}
						}
						else
						{
							auto centerVec = centerWS - kp->cp;
							float camDepth = centerVec.dot(kp->camForward);

							// 影方向とカメラ Forward の cosθ
							float cosLF = std::fabs(kp->shadowDir.dot(kp->camForward)); // 0..1

							// 影としてどこまで考慮するか（ワールド距離）
							// 例：一番遠いカスケードの far クリップ距離 - near 距離
							float maxShadowLenWS = kp->maxShadowDistance;

							// カメラ深度方向に投影される「影の余白」
							// 光がカメラとほぼ平行だと cosLF ≈ 1 になって深く伸びる
							float extraDepth = bsRadiusWS + maxShadowLenWS * cosLF;

							// これでカスケード範囲を決める
							float minD = camDepth - extraDepth;
							float maxD = camDepth + extraDepth;

							auto cascades = kp->lightShadowService->GetCascadeIndexRangeUnlock(minD, maxD);

							for (uint32_t ci = cascades.first; ci <= cascades.second; ++ci)
							{
								cmd.viewMask |= (PASS_3DMAIN_CASCADE0 << ci);
							}
						}


						cmd.sortKey = Graphics::MakeSortKey(mesh.pso.index, mesh.material.index, meshHandel.index);
						producer.Push(std::move(cmd));

						subMeshIdx++;
						if (subMeshIdx >= 16) [[unlikely]] {
							LOG_ERROR("モデルのサブメッシュ数が多すぎます {%d}", modelAsset.subMeshes.size());
							return;
						}
					}
				}
			}, partition, fru, camPos, &kp, threadPool.get()
		);

#if PROFILE_MODEL_UPDATE_TIME
		clock_t end = clock();

		const double time = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000.0;

		static int displayCount = 0;
		static double totalTime = 0.0;

		totalTime += time;

		displayCount++;
		if (displayCount % 60 == 0)
		{
			std::string msg = std::format("[Profile] ModelRenderSystem Update Time: {:.3f} ms", totalTime / 60.0);
			Debug::PushLog(std::move(msg));

			displayCount = 0;
			totalTime = 0.0;
		}
#endif
	}

private:
	Math::AABB3f occluderAABBs[MAX_OCCLUDER_AABB_NUM];
	std::atomic<uint32_t> occluderAABBCount{ 0 };
};
