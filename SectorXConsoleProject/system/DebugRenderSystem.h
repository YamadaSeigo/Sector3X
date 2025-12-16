#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Debug/DebugType.h>
#include <SectorFW/Math/aabb_util.h>
#include <SectorFW/Math/Rectangle.hpp>

#include <SectorFW/Graphics/OccluderToolkit.h>
#include <SectorFW/Graphics/LightShadowService.h>

#include "ModelRenderSystem.h"

using namespace SFW;

// 24頂点+36インデックスを生成（中心原点、寸法 w,h,d）
void MakeBox(float w, float h, float d,
	std::vector<Debug::VertexPNUV>& outVerts,
	std::vector<uint32_t>& outIndices);

void MakeBoxLines(float w, float h, float d,
	std::vector<Debug::LineVertex>& outVerts,
	std::vector<uint32_t>& outIndices);

// 頂点/インデックス生成（UV 球）
//  radius: 半径
//  slices: 経度分割数（最小 3）
//  stacks: 緯度分割数（最小 2）
// 生成結果は CW（時計回り）で表面になるインデックス
void MakeSphere(float radius, uint32_t slices, uint32_t stacks,
	std::vector<Debug::VertexPNUV>& outVerts,
	std::vector<uint32_t>& outIndices);

enum class CirclePlane { XY, XZ, YZ };

// segments: 8 以上推奨。LINELIST なので閉ループは (i, i+1), (last, first) を張る
void AppendCircle(float radius, uint32_t segments, CirclePlane plane,
	std::vector<Debug::LineVertex>& verts, std::vector<uint32_t>& idx,
	float yOffset = 0.0f, float rotY = 0.0f);

// 十字（3 本）のみ
void MakeSphereCrossLines(float radius, uint32_t segments,
	std::vector<Debug::LineVertex>& outVerts,
	std::vector<uint32_t>& outIndices,
	bool addXY = true, bool addYZ = true, bool addXZ = true);

void MakeCapsuleLines(float radius, float halfHeight,
	uint32_t meridianSegments, uint32_t ringSegments,
	std::vector<Debug::LineVertex>& outVerts,
	std::vector<uint32_t>& outIndices);

template<typename Partition>
class DebugRenderSystem : public ITypeSystem<
	DebugRenderSystem<Partition>,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<Physics::ShapeDims>,
		Read<Physics::PhysicsInterpolation>,
		Read<TransformSoA>,
		Read<CModel>
	>,
	//受け取るサービスの指定
	ServiceContext<
		Graphics::RenderService,
		Graphics::I3DPerCameraService,
		Graphics::I2DCameraService,
		Graphics::LightShadowService,
		Physics::PhysicsService
	>>
{
	using ShapeDimsAccessor = ComponentAccessor<Read<Physics::ShapeDims>, Read<CTransform>>;
	using ModelAccessor = ComponentAccessor<Read<TransformSoA>, Read<CModel>>;

	static constexpr inline uint32_t MAX_CAPACITY_3DLINE = 65536 * 2;
	static constexpr inline uint32_t MAX_CAPACITY_3DVERTEX = MAX_CAPACITY_3DLINE * 2;

	static constexpr inline uint32_t MAX_CAPACITY_2DLINE = 65536 / 4;
	static constexpr inline uint32_t MAX_CAPACITY_2DVERTEX = MAX_CAPACITY_2DLINE * 2;

	static constexpr inline uint32_t DRAW_LINE_CHUNK_COUNT = 12;
public:
	void StartImpl(
		UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> camera3DService,
		UndeletablePtr<Graphics::I2DCameraService>,
		UndeletablePtr<Graphics::LightShadowService>,
		UndeletablePtr <Physics::PhysicsService>)
	{
		using namespace Graphics;
		using namespace Debug;

		auto meshMgr = renderService->GetResourceManager<DX11::MeshManager>();

		std::vector<LineVertex> boxVerts;
		std::vector<uint32_t> boxIndices;

		MakeBoxLines(1.0f, 1.0f, 1.0f, boxVerts, boxIndices);
		DX11::MeshCreateDesc boxDesc{
			.vertices = boxVerts.data(),
			.vSize = (uint32_t)boxVerts.size() * sizeof(LineVertex),
			.stride = sizeof(LineVertex),
			.indices = boxIndices.data(),
			.iSize = (uint32_t)boxIndices.size() * sizeof(uint32_t),
			.sourcePath = L"__internal__/Box"
		};

		meshMgr->Add(boxDesc, boxHandle);

		std::vector<LineVertex> sphereVerts;
		std::vector<uint32_t> sphereIndices;

		constexpr float radius = 0.5f;
		constexpr uint32_t segment = 4;

		//MakeSphere(0.5f, 8, 8, sphereVerts, sphereIndices);
		MakeSphereCrossLines(radius, segment * 4, sphereVerts, sphereIndices, true, true, true);
		DX11::MeshCreateDesc sphereDesc{
			.vertices = sphereVerts.data(),
			.vSize = (uint32_t)sphereVerts.size() * sizeof(LineVertex),
			.stride = sizeof(LineVertex),
			.indices = sphereIndices.data(),
			.iSize = (uint32_t)sphereIndices.size() * sizeof(uint32_t),
			.sourcePath = L"__internal__/Sphere"
		};
		meshMgr->Add(sphereDesc, sphereHandle);

		std::vector<LineVertex> capsuleLineVerts;
		std::vector<uint32_t> capsuleLineIndices;
		{
			capsuleLineVerts.reserve(segment * 2);
			capsuleLineIndices.reserve(segment * 2);
			for (int i = 0; i < segment; ++i)
			{
				float rad = Math::tau_v<float> / segment * i;
				float rx = cos(rad) * radius;
				float rz = sin(rad) * radius;
				capsuleLineVerts.push_back({ {rx, 0.5f, rz} });
				capsuleLineVerts.push_back({ { rx, -0.5f, rz } });

				capsuleLineIndices.push_back(i * 2 + 0);
				capsuleLineIndices.push_back(i * 2 + 1);
			}
		}

		DX11::MeshCreateDesc capsuleLineDesc{
		.vertices = capsuleLineVerts.data(),
		.vSize = (uint32_t)capsuleLineVerts.size() * sizeof(LineVertex),
		.stride = sizeof(LineVertex),
		.indices = capsuleLineIndices.data(),
		.iSize = (uint32_t)capsuleLineIndices.size() * sizeof(uint32_t),
		.sourcePath = L"__internal__/CapsuleLine"
		};
		meshMgr->Add(capsuleLineDesc, capsuleLineHandle);

		auto shaderMgr = renderService->GetResourceManager<DX11::ShaderManager>();
		DX11::ShaderCreateDesc shaderDesc;
		shaderDesc.templateID = MaterialTemplateID::PBR;
		shaderDesc.vsPath = L"assets/shader/VS_DrawLineList.cso";
		shaderDesc.psPath = L"assets/shader/PS_DrawLineList.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		auto psoMgr = renderService->GetResourceManager<DX11::PSOManager>();
		DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::WireCullNone };
		psoMgr->Add(psoDesc, psoLineHandle);

		ShaderHandle mocShaderHandle;
		shaderDesc.vsPath = L"assets/shader/VS_Unlit.cso";
		shaderDesc.psPath = L"assets/shader/PS_MOCDebug.cso";
		shaderMgr->Add(shaderDesc, mocShaderHandle);
		psoDesc = { mocShaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(psoDesc, psoMOCHandle);

		// --- Index Buffer（固定：0,1,2,3,4,5,…）---
		std::vector<uint32_t> indices(MAX_CAPACITY_3DLINE);
		for (uint32_t i = 0; i < MAX_CAPACITY_3DLINE; ++i) indices[i] = i;

		DX11::MeshCreateDesc lineDesc;
		lineDesc.vertices = nullptr;
		lineDesc.vSize = sizeof(LineVertex) * MAX_CAPACITY_3DVERTEX;
		lineDesc.stride = sizeof(LineVertex);
		lineDesc.vUsage = D3D11_USAGE_DYNAMIC;
		lineDesc.indices = indices.data();
		lineDesc.iSize = sizeof(uint32_t) * (uint32_t)indices.size();
		lineDesc.sourcePath = L"__internal__/Line3DBuffer";
		meshMgr->Add(lineDesc, line3DHandle);

		line3DVertices.reset(new LineVertex[MAX_CAPACITY_3DLINE]);

		lineDesc.vSize = sizeof(LineVertex) * MAX_CAPACITY_2DVERTEX;
		lineDesc.iSize = sizeof(uint32_t) * MAX_CAPACITY_2DLINE;
		lineDesc.sourcePath = L"__internal__/Line2DBuffer";
		meshMgr->Add(lineDesc, line2DHandle);

		line2DVertices.reset(new LineVertex[MAX_CAPACITY_2DLINE]);

		auto texMgr = renderService->GetResourceManager<DX11::TextureManager>();

		auto resolution = camera3DService->GetResolution();

		uint32_t width = (UINT)resolution.x;
		uint32_t height = (UINT)resolution.y;

		mocDepth.resize(width * height);

		Graphics::DX11::TextureRecipe recipe =
		{
		.width = width,
		.height = height,
		.format = DXGI_FORMAT_R32_FLOAT,
		.mipLevels = 1,
		.arraySize = 1,
		.usage = D3D11_USAGE_DEFAULT,
		.bindFlags = D3D11_BIND_SHADER_RESOURCE, // 必要に応じて RENDER_TARGET など追加
		.cpuAccessFlags = 0,
		.miscFlags = 0,
		.initialData = mocDepth.data(),
		.initialRowPitch = (UINT)width * sizeof(float)
		};

		DX11::TextureCreateDesc texDesc;
		texDesc.forceSRGB = false;
		texDesc.recipe = &recipe;
		texMgr->Add(texDesc, mocTexHandle);

		Graphics::DX11::MaterialCreateDesc matDesc;
		matDesc.shader = mocShaderHandle;
		matDesc.psSRV[10] = mocTexHandle; // TEX10 にセット
		auto matMgr = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		matMgr->Add(matDesc, mocMaterialHandle);

		//imguiにバインド
		BIND_DEBUG_CHECKBOX("Show", "enabled", &enabled);
		BIND_DEBUG_CHECKBOX("Show", "partition", &drawPartitionBounds);
		BIND_DEBUG_CHECKBOX("Show", "modelAABB", &drawModelAABB);
		BIND_DEBUG_CHECKBOX("Show", "occAABB", &drawOccluderAABB);
		BIND_DEBUG_CHECKBOX("Show", "modelRect", &drawModelRect);
		BIND_DEBUG_CHECKBOX("Show", "occlutionRect", &drawOcclutionRect);
		BIND_DEBUG_CHECKBOX("Show", "cascadesAABB", &drawCascadeAABB);
		BIND_DEBUG_CHECKBOX("Show", "shapeDims", &drawShapeDims);
		BIND_DEBUG_CHECKBOX("Show", "MOCDepth", &drawMOCDepth);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DPerCameraService> camera3DService,
		UndeletablePtr<Graphics::I2DCameraService> camera2DService,
		UndeletablePtr<Graphics::LightShadowService> lightShadowService,
		UndeletablePtr <Physics::PhysicsService> physicsService)
	{
		if (!enabled) return;

		//機能を制限したRenderQueueを取得
		auto uiSession = renderService->GetProducerSession(PassGroupName[GROUP_UI]);
		auto* meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();
		auto modelManager = renderService->GetResourceManager<Graphics::DX11::ModelAssetManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11::PSOManager>();
		auto bufferManager = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
		if (!psoManager->IsValid(psoLineHandle)) {
			LOG_ERROR("PSOHandle が有効な値ではありません");
			return;
		}

		auto fru = camera3DService->MakeFrustum();

		auto cameraPos = camera3DService->GetEyePos();
		const auto viewProj = camera3DService->GetCameraBufferData().viewProj;
		auto fov = camera3DService->GetFOV();

		Math::Vec2f resolution = camera2DService->GetVirtualResolution();

		const Graphics::OccluderViewport vp = { (int)resolution.x, (int)resolution.y, fov };

		auto line3DCount = 0;
		if (drawPartitionBounds)
		{
			line3DCount = partition.CullChunkLine(fru, cameraPos, 200.0f,
				line3DVertices.get(), MAX_CAPACITY_3DLINE, DRAW_LINE_CHUNK_COUNT);
		}

		if (drawCascadeAABB)
		{
			const auto& cascade = lightShadowService->GetCascades();
			uint32_t cascadeCount = (uint32_t)cascade.boundsWS.size();
			for (uint32_t i = 0; i < cascadeCount; ++i)
			{
				const auto& aabb = cascade.boundsWS[i];
				float t = (float(i) / float(cascadeCount - 1));
				auto lineVertex = Math::MakeAABBLineVertices(aabb, Math::LerpColor(0xFF0000FF, 0x0000FFFF, t));
				size_t newLineSize = lineVertex.size();
				if (line3DCount + newLineSize > MAX_CAPACITY_3DLINE) {
					break;
				}
				for (auto& l : lineVertex) {
					line3DVertices.get()[line3DCount++] = { l.pos, l.rgba };
				}
			}
		}

		uint32_t line2DCount = 0;

		if (drawModelAABB || drawOccluderAABB || drawModelRect || drawOcclutionRect)
		{

			this->ForEachFrustumChunkWithAccessor<ModelAccessor>([&](ModelAccessor& accessor, size_t entityCount)
				{
					//読み取り専用でTransformSoAのアクセサを取得
					auto transform = accessor.Get<Read<TransformSoA>>();
					auto model = accessor.Get<Read<CModel>>();

					bool overflow = false;
					for (size_t i = 0; i < entityCount; ++i) {
						if (overflow) return;

						Math::Vec3f pos(transform->px()[i], transform->py()[i], transform->pz()[i]);
						Math::Quatf rot(transform->qx()[i], transform->qy()[i], transform->qz()[i], transform->qw()[i]);
						Math::Vec3f scale(transform->sx()[i], transform->sy()[i], transform->sz()[i]);
						auto transMtx = Math::MakeTranslationMatrix(pos);
						auto rotMtx = Math::MakeRotationMatrix(rot);
						auto scaleMtx = Math::MakeScalingMatrix(scale);
						//ワールド行列を計算
						auto worldMtx = transMtx * rotMtx * scaleMtx;

						//モデルアセットを取得
						auto modelAsset = modelManager->Get(model.value()[i].handle);

						float dis = (pos - cameraPos).length();
						if (dis > 200.0f) continue; // 遠すぎる
						float alpha = 1.0f - (dis / 200.0f);
						auto rgbaAABB = Math::LerpColor(0x00000ff, 0x00ff00ff, alpha);
						auto rgbaRect = Math::LerpColor(0x00000ff, 0xffff00ff, alpha);
						auto rgbaOcc = Math::LerpColor(0x000000ff, 0xff00ffff, alpha);
						auto rgbaOccQuad = Math::LerpColor(0x000000ff, 0xFF0000ff, alpha);

						auto& lodBits = model.value()[i].prevLODBits;
						int subMeshIdx = 0;

						std::vector<Math::Vec3f> linePoss;
						std::vector<uint32_t> lineColors;
						for (const auto& mesh : modelAsset.ref().subMeshes) {

							Math::Rectangle rect = Math::ProjectAABBToScreenRect(mesh.aabb, viewProj * worldMtx, resolution.x, resolution.y, -resolution.x * 0.5f, -resolution.y * 0.5f);
							//2D Rect
							if (drawModelRect || drawOcclutionRect)
							{
								if (model.value()[i].occluded || drawModelRect)
								{
									if (rect.Area() > 0.0f)
									{
										auto rectLines = rect.MakeLineVertex();
										if ((size_t)line2DCount + rectLines.size() > MAX_CAPACITY_2DLINE) {
											overflow = true;
											break;
										}
										for (auto& l : rectLines) {
											line2DVertices.get()[line2DCount++] = { Math::Vec3f(l.x, l.y, 5.0f), rgbaRect };
										}
									}
								}
							}

							//3D AABB
							if (drawModelAABB)
							{
								auto lines = Math::MakeAABBLineVertices(mesh.aabb, rgbaAABB);
								size_t newLineSize = lines.size();
								if (mesh.occluder.candidate)
								{
									if (drawOccluderAABB)
										newLineSize += mesh.occluder.meltAABBs.size() * 24; // OCcluder AABB 分
								}

								if ((size_t)line3DCount + linePoss.size() + newLineSize > MAX_CAPACITY_3DLINE) {
									overflow = true;
									break;
								}

								linePoss.reserve(linePoss.size() + newLineSize);
								lineColors.reserve(lineColors.size() + newLineSize);

								for (auto& l : lines) {
									linePoss.push_back(l.pos);
									lineColors.push_back(l.rgba);
								}
							}

							//OCcluder AABB
							if (mesh.occluder.candidate && drawOccluderAABB)
							{
								for (const auto& aabb : mesh.occluder.meltAABBs)
								{
									auto occLines = Math::MakeAABBLineVertices(aabb, rgbaOcc);
									for (auto& l : occLines) {
										linePoss.push_back(l.pos);
										lineColors.push_back(l.rgba);
									}
								}

								int prevLod = (int)lodBits.get(subMeshIdx);
								float s_occ = Graphics::ScreenCoverageFromRectPx(rect.x0, rect.y0, rect.x1, rect.y1, resolution.x, resolution.y);
								auto occLod = Graphics::DecideOccluderLOD_FromThresholds(s_occ, mesh.lodThresholds, prevLod, prevLod);

								size_t occCount = mesh.occluder.meltAABBs.size();
								std::vector<Math::AABB3f> occAABB(occCount);
								for (auto i = 0; i < occCount; ++i)
								{
									occAABB[i] = Math::TransformAABB_Affine(worldMtx, mesh.occluder.meltAABBs[i]);
								}

								//auto outQuad = Math::CollectFacingPlanes(occAABB, cameraPos, viewProj);

								std::vector<Graphics::QuadCandidate> outQuad;
								Graphics::SelectOccluderQuads_AVX2(
									occAABB.data(),
									occAABB.size(),
									cameraPos,
									viewProj,
									vp,
									occLod,
									outQuad);

								if ((size_t)line3DCount + outQuad.size() * 8 > MAX_CAPACITY_3DLINE) {
									overflow = true;
									break;
								}

								for (const auto& quad : outQuad)
								{
									line3DVertices.get()[line3DCount++] = { quad.quad.v[0], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[1], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[1], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[2], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[2], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[3], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[3], rgbaOccQuad };
									line3DVertices.get()[line3DCount++] = { quad.quad.v[0], rgbaOccQuad };
								}
							}

							subMeshIdx++;
						}
						// 3dラインをワールド変換して格納
						if (linePoss.empty()) continue;

						std::vector<Math::Vec3f> outPoss(linePoss.size());
						Math::TransformPoints(worldMtx, linePoss.data(), outPoss.data(), linePoss.size());

						for (size_t v = 0; v < outPoss.size(); ++v) {
							line3DVertices.get()[line3DCount + v] = { outPoss[v], lineColors[v] };
						}

						line3DCount += (uint32_t)outPoss.size();
					}

				}, partition, fru);
		}

		if (drawShapeDims)
		{
			this->ForEachFrustumNearChunkWithAccessor<ShapeDimsAccessor>([&](ShapeDimsAccessor& accessor, size_t entityCount, auto meshMgr, auto queue, auto pso, auto boxMesh, auto sphereMesh, auto capsuleLineMesh)
				{
					auto shapeDims = accessor.Get<Read<Physics::ShapeDims>>();
					auto tf = accessor.Get<Read<CTransform>>();

					if (!shapeDims) return;
					if (!tf) return;

					for (int i = 0; i < entityCount; ++i) {
						auto transMtx = Math::MakeTranslationMatrix(Math::Vec3f(tf->px()[i], tf->py()[i], tf->pz()[i]));
						auto rotMtx = Math::MakeRotationMatrix(Math::Quatf(tf->qx()[i], tf->qy()[i], tf->qz()[i], tf->qw()[i]));

						auto& d = shapeDims.value()[i];
						switch (d.type) {
						case Physics::ShapeDims::Type::Box:
						{
							auto mtx = transMtx * rotMtx * Math::MakeScalingMatrix(d.dims);
							Graphics::DrawCommand cmd;
							cmd.instanceIndex = queue->AllocInstance({ mtx });
							cmd.mesh = boxMesh;
							cmd.material = 0;
							cmd.pso = pso;
							cmd.sortKey = 0; // 適切なソートキーを設定
							cmd.viewMask = PASS_UI_3DLINE;
							queue->Push(std::move(cmd));
							break;
						}
						case Physics::ShapeDims::Type::Sphere:
						{
							auto mtx = transMtx * rotMtx * Math::MakeScalingMatrix(Math::Vec3f(d.r * 2)); // 球は均一スケーリング
							Graphics::DrawCommand cmd;
							cmd.instanceIndex = queue->AllocInstance({ mtx });
							cmd.mesh = sphereMesh;
							cmd.material = 0;
							cmd.pso = pso;
							cmd.sortKey = 0; // 適切なソートキーを設定
							cmd.viewMask = PASS_UI_3DLINE;
							queue->Push(std::move(cmd));
							break;
						}
						case Physics::ShapeDims::Type::Capsule:
						{
							auto offset = d.localOffset;
							offset.y += d.halfHeight;
							auto offsetMtx = Math::MakeTranslationMatrix(offset);
							auto scaleMtx = Math::MakeScalingMatrix(Math::Vec3f(d.r * 2));
							auto instMtx = transMtx * rotMtx;
							auto mtx = instMtx * offsetMtx * scaleMtx; // 球は均一スケーリング
							Graphics::DrawCommand cmd;
							cmd.instanceIndex = queue->AllocInstance({ mtx });
							cmd.mesh = sphereMesh;
							cmd.material = 0;
							cmd.pso = pso;
							cmd.sortKey = 0; // 適切なソートキーを設定
							cmd.viewMask = PASS_UI_3DLINE;
							queue->Push(cmd);

							offset = d.localOffset;
							offset.y -= d.halfHeight;
							offsetMtx = Math::MakeTranslationMatrix(offset);
							mtx = instMtx * offsetMtx * scaleMtx;
							cmd.instanceIndex = queue->AllocInstance({ mtx });
							queue->Push(cmd);

							// 真ん中の線
							offset = d.localOffset;
							offsetMtx = Math::MakeTranslationMatrix(offset);
							scaleMtx = Math::MakeScalingMatrix(Math::Vec3f(d.r * 2, d.halfHeight * 2, d.r * 2));
							mtx = instMtx * offsetMtx * scaleMtx;
							cmd.mesh = capsuleLineMesh;
							cmd.instanceIndex = queue->AllocInstance({ mtx });
							queue->Push(std::move(cmd));

							break;
						}
#ifdef CACHE_SHAPE_WIRE_DATA
						case  Physics::ShapeDims::Type::CMHC:
						{
							auto wireData = physicsService->GetShapeWireframeData(d.handle);
							if (wireData.has_value())
							{
								//とりあえずTransformのScaleを使用する。本来はShapeDimsにスケール情報を持たせるべき
								auto mtx = transMtx * rotMtx;
								const Physics::WireframeData& wire = wireData->data;
								std::vector<Math::Vec3f> worldPos(wire.vertices.size());
								Math::TransformPoints(mtx, wire.vertices.data(), worldPos.data(), wire.vertices.size());
								for (size_t vi = 0; vi < wire.indices.size(); vi += 2)
								{
									if (line3DCount + 2 > MAX_CAPACITY_3DLINE) break;
									auto v0 = worldPos[wire.indices[vi + 0]];
									auto v1 = worldPos[wire.indices[vi + 1]];
									line3DVertices.get()[line3DCount++] = { v0, 0xffffffff };
									line3DVertices.get()[line3DCount++] = { v1, 0xffffffff };
								}
							}
							break;
						}
#endif
						default:
							break;
						}
					}
				}, partition, fru, cameraPos, meshManager, &uiSession, psoLineHandle.index, boxHandle.index, sphereHandle.index, capsuleLineHandle.index);
		}

		if (drawMOCDepth)
		{
			renderService->GetDepthBuffer(mocDepth);

			Graphics::DX11::TextureManager* texMgr = renderService->GetResourceManager<Graphics::DX11::TextureManager>();

			texMgr->UpdateTexture(mocTexHandle, mocDepth.data(), (UINT)(camera3DService->GetResolution().x) * sizeof(float));

			Math::Matrix4x4f transMat = Math::MakeTranslationMatrix(Math::Vec3f(350.0f, -220.0f, 0.0f));
			Math::Matrix4x4f scaleMat = Math::MakeScalingMatrix(Math::Vec3f{ 1920.0f / 5.0f, 1080.0f / 5.0f, 1.0f });

			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance(transMat * scaleMat);
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = psoMOCHandle.index;
			cmd.material = mocMaterialHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;

			uiSession.Push(std::move(cmd));
		}

		if (line3DCount > 0)
		{
			Graphics::DX11::BufferUpdateDesc vbUpdateDesc;
			{
				auto lineBuffer = meshManager->Get(line3DHandle);
				vbUpdateDesc.buffer = lineBuffer.ref().vbs[0];
			}

			auto slot = renderService->GetProduceSlot();

			meshManager->SetIndexCount(line3DHandle, (uint32_t)line3DCount);
			vbUpdateDesc.data = line3DVertices.get();
			vbUpdateDesc.size = sizeof(Debug::LineVertex) * line3DCount;
			vbUpdateDesc.isDelete = false; // 更新時は削除
			bufferManager->UpdateBuffer(vbUpdateDesc, slot);

			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance({ Math::Matrix4x4f::Identity() });
			cmd.mesh = line3DHandle.index;
			cmd.material = 0;
			cmd.pso = psoLineHandle.index;
			cmd.sortKey = 0; // 本来は適切なソートキーを設定
			cmd.viewMask = PASS_UI_3DLINE;
			uiSession.Push(cmd);
		}

		if (line2DCount > 0)
		{
			Graphics::DX11::BufferUpdateDesc vbUpdateDesc;
			{
				auto lineBuffer = meshManager->Get(line2DHandle);
				vbUpdateDesc.buffer = lineBuffer.ref().vbs[0];
			}
			meshManager->SetIndexCount(line2DHandle, (uint32_t)line2DCount);
			vbUpdateDesc.data = line2DVertices.get();
			vbUpdateDesc.size = sizeof(Debug::LineVertex) * line2DCount;
			vbUpdateDesc.isDelete = false; // 更新時は削除しない

			auto slot = renderService->GetProduceSlot();

			bufferManager->UpdateBuffer(vbUpdateDesc, slot);

			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance({ Math::Matrix4x4f::Identity() });
			cmd.mesh = line2DHandle.index;
			cmd.material = 0;
			cmd.pso = psoLineHandle.index;
			cmd.viewMask = PASS_UI_LINE;
			cmd.sortKey = 0; // 本来は適切なソートキーを設定
			uiSession.Push(cmd);
		}
	}
private:
	bool enabled = false;
	bool drawPartitionBounds = false;
	bool drawModelAABB = false;
	bool drawOccluderAABB = false;
	bool drawModelRect = false;
	bool drawOcclutionRect = false;
	bool drawCascadeAABB = false;
	bool drawShapeDims = false;
	bool drawMOCDepth = false;

	Graphics::PSOHandle psoLineHandle = {};
	Graphics::PSOHandle psoMOCHandle = {};
	Graphics::MeshHandle line3DHandle = {};
	Graphics::MeshHandle line2DHandle = {};
	std::unique_ptr<Debug::LineVertex> line3DVertices;
	std::unique_ptr<Debug::LineVertex> line2DVertices;

	Graphics::TextureHandle mocTexHandle = {};
	Graphics::MaterialHandle mocMaterialHandle = {};

	std::vector<float> mocDepth;

	Graphics::MeshHandle boxHandle = {}; // デフォルトメッシュ（立方体）
	Graphics::MeshHandle sphereHandle = {}; // デフォルトメッシュ（球）
	Graphics::MeshHandle capsuleLineHandle = {}; //カプセルの真ん中の直線
};
