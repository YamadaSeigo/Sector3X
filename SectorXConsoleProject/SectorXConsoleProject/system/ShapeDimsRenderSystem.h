#pragma once

#include <SectorFW/Physics/PhysicsComponent.h>
#include <SectorFW/Debug/DebugType.h>

using namespace SectorFW;

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

template<typename Partition>
class ShapeDimsRenderSystem : public ITypeSystem<
	ShapeDimsRenderSystem<Partition>,
	Partition,
	ComponentAccess<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>,//アクセスするコンポーネントの指定
	ServiceContext<Graphics::RenderService, Graphics::I3DCameraService>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<Read<Physics::ShapeDims>, Read<Physics::PhysicsInterpolation>>;

	static constexpr inline uint32_t MAX_CAPACITY_LINE = 65536;
	static constexpr inline uint32_t MAX_CAPACITY_VERTEX = MAX_CAPACITY_LINE * 2;
	static constexpr inline uint32_t DRAW_LINE_CHUNK_COUNT = 12;
public:
	void StartImpl(UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DCameraService> cameraService) {
		using namespace Graphics;
		using namespace Debug;

		auto meshMgr = renderService->GetResourceManager<DX11MeshManager>();

		std::vector<LineVertex> boxVerts;
		std::vector<uint32_t> boxIndices;

		MakeBoxLines(1.0f, 1.0f, 1.0f, boxVerts, boxIndices);
		DX11MeshCreateDesc boxDesc{
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

		//MakeSphere(0.5f, 8, 8, sphereVerts, sphereIndices);
		MakeSphereCrossLines(0.5f, 16, sphereVerts, sphereIndices, true, true, true);
		DX11MeshCreateDesc sphereDesc{
			.vertices = sphereVerts.data(),
			.vSize = (uint32_t)sphereVerts.size() * sizeof(LineVertex),
			.stride = sizeof(LineVertex),
			.indices = sphereIndices.data(),
			.iSize = (uint32_t)sphereIndices.size() * sizeof(uint32_t),
			.sourcePath = L"__internal__/Sphere"
		};

		meshMgr->Add(sphereDesc, sphereHandle);

		auto shaderMgr = renderService->GetResourceManager<DX11ShaderManager>();
		DX11ShaderCreateDesc shaderDesc;
		shaderDesc.templateID = MaterialTemplateID::PBR;
		shaderDesc.vsPath = L"asset/shader/VS_DrawLineList.cso";
		shaderDesc.psPath = L"asset/shader/PS_DrawLineList.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		auto psoMgr = renderService->GetResourceManager<DX11PSOManager>();
		DX11PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::WireCullNone };
		psoMgr->Add(psoDesc, psoHandle);

		// --- Index Buffer（固定：0,1,2,3,4,5,…）---
		std::vector<uint32_t> indices(MAX_CAPACITY_LINE);
		for (uint32_t i = 0; i < MAX_CAPACITY_LINE; ++i) indices[i] = i;

		DX11MeshCreateDesc lineDesc;
		lineDesc.vertices = nullptr;
		lineDesc.vSize = sizeof(LineVertex) * MAX_CAPACITY_VERTEX;
		lineDesc.stride = sizeof(LineVertex);
		lineDesc.vUsage = D3D11_USAGE_DYNAMIC;
		lineDesc.indices = indices.data();
		lineDesc.iSize = sizeof(uint32_t) * (uint32_t)indices.size();
		lineDesc.sourcePath = L"__internal__/LineBuffer";
		meshMgr->Add(lineDesc, lineHandle);

		lineVertices.reset(new LineVertex[MAX_CAPACITY_LINE]);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition, UndeletablePtr<Graphics::RenderService> renderService,
		UndeletablePtr<Graphics::I3DCameraService> cameraService) {
		//機能を制限したRenderQueueを取得
		auto producerSession = renderService->GetProducerSession("DrawLine");
		auto meshManager = renderService->GetResourceManager<Graphics::DX11MeshManager>();
		auto psoManager = renderService->GetResourceManager<Graphics::DX11PSOManager>();
		auto bufferManager = renderService->GetResourceManager<Graphics::DX11BufferManager>();
		if (!psoManager->IsValid(psoHandle)) {
			LOG_ERROR("PSOHandle is not valid in ShapeDimsRenderSystem");
			return;
		}

		ForEachFrustumDesc frustumDesc = {};
		frustumDesc.fru = cameraService->MakeFrustum();

		auto cameraPos = cameraService->GetPosition();

		auto lineCount = partition.CullChunkLine(frustumDesc.fru, cameraPos, 200.0f,
			lineVertices.get(), MAX_CAPACITY_LINE, DRAW_LINE_CHUNK_COUNT);

		Graphics::DX11BufferUpdateDesc vbUpdateDesc;
		{
			auto lineBuffer = meshManager->Get(lineHandle);
			vbUpdateDesc.buffer = lineBuffer.ref().vb;
		}

		meshManager->SetIndexCount(lineHandle, (uint32_t)lineCount);
		vbUpdateDesc.data = lineVertices.get();
		vbUpdateDesc.size = sizeof(Debug::LineVertex) * lineCount;
		vbUpdateDesc.isDelete = false; // 更新時は削除
		bufferManager->UpdateBuffer(vbUpdateDesc);

		Graphics::DrawCommand cmd;
		cmd.instanceIndex = producerSession.AllocInstance({ Math::Matrix4x4f::Identity() });
		cmd.mesh = lineHandle.index;
		cmd.material = 0;
		cmd.pso = psoHandle.index;
		cmd.sortKey = 0; // 適切なソートキーを設定
		producerSession.Push(std::move(cmd));

		this->ForEachChunkWithAccessor([](Accessor& accessor, size_t entityCount, auto meshMgr, auto queue, auto pso, auto boxMesh, auto sphereMesh)
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
						cmd.mesh = boxMesh;
						cmd.material = 0;
						cmd.pso = pso;
						cmd.sortKey = 0; // 適切なソートキーを設定
						queue->Push(std::move(cmd));
						break;
					}
					case Physics::ShapeDims::Type::Sphere: {
						auto mtx = transMtx * rotMtx * Math::MakeScalingMatrix(Math::Vec3f(d.r * 2)); // 球は均一スケーリング
						Graphics::DrawCommand cmd;
						cmd.instanceIndex = queue->AllocInstance({ mtx });
						cmd.mesh = sphereMesh;
						cmd.material = 0;
						cmd.pso = pso;
						cmd.sortKey = 0; // 適切なソートキーを設定
						queue->Push(std::move(cmd));
						break;
					}
					default:
						break;
					}
				}
			}, partition, meshManager, &producerSession, psoHandle.index, boxHandle.index, sphereHandle.index);
	}
private:
	Graphics::PSOHandle psoHandle = {};
	Graphics::MeshHandle lineHandle = {};
	std::unique_ptr<Debug::LineVertex> lineVertices;

	Graphics::MeshHandle boxHandle = {}; // デフォルトメッシュ（立方体）
	Graphics::MeshHandle sphereHandle = {}; // デフォルトメッシュ（球）
};
