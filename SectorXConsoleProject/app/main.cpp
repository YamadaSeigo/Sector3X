
//SectorFW
#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include <SectorFW/DX11WinTerrainHelper.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>
#include <SectorFW/Graphics/TerrainOccluderExtraction.h>

//System
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/DebugRenderSystem.h"
#include "system/TestMoveSystem.h"
#include "system/CleanModelSystem.h"
#include "system/SimpleModelRenderSystem.h"
#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

#define WINDOW_NAME "SectorX Console Project"

constexpr uint32_t WINDOW_WIDTH = 960;	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = 720;	// ウィンドウの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限

enum : uint32_t {
	Mat_Grass = 1, Mat_Rock = 2, Mat_Dirt = 3, Mat_Snow = 4,
	Tex_Splat_Control_0 = 10001,
};

struct MaterialRecord {
	std::string albedoPath;
	bool        albedoSRGB = true;   // カラーなので true を推奨
	// 将来用: std::string normalPath; bool normalSRGB=false;
	// 将来用: std::string ormPath;    bool ormSRGB=false;
};

static std::unordered_map<uint32_t, MaterialRecord> gMaterials = {
	{ Mat_Grass, { "assets/texture/terrain/grass.png", true } },
	{ Mat_Rock,  { "assets/texture/terrain/rock.png",  true } },
	{ Mat_Dirt,  { "assets/texture/terrain/dirt.png",  true } },
	{ Mat_Snow,  { "assets/texture/terrain/snow.png",  true } },
};

// 素材以外（スプラット重み等）は “テクスチャID” テーブルで受ける
static std::unordered_map<uint32_t, std::pair<std::string, bool>> gTextures = {
	// 重みテクスチャは “非 sRGB” 推奨（正規化済みのスカラー重みだから）
	{ Tex_Splat_Control_0, { "assets/texture/terrain/splat.png", false } },
};

// BuildClusterSplatSRVs に渡す
static bool ResolveTexturePath(uint32_t id, std::string& path, bool& forceSRGB)
{
	if (auto it = gMaterials.find(id); it != gMaterials.end()) {
		path = it->second.albedoPath;
		forceSRGB = it->second.albedoSRGB;
		return true;
	}
	if (auto it2 = gTextures.find(id); it2 != gTextures.end()) {
		path = it2->second.first;
		forceSRGB = it2->second.second;
		return true;
	}
	return false; // 未登録ID
}

int main(void)
{
	LOG_INFO("SectorX Console Project started");

	//==コンポーネントの登録=====================================
	//main.cppに集めた方がコンパイル効率がいいので、ここで登録している
	//※複数人で開発する場合は、各自のコンポーネントを別ファイルに分けて登録するようにする
	ComponentTypeRegistry::Register<CModel>();
	ComponentTypeRegistry::Register<TransformSoA>();
	ComponentTypeRegistry::Register<SpatialMotionTag>();
	ComponentTypeRegistry::Register<Physics::BodyComponent>();
	ComponentTypeRegistry::Register<Physics::PhysicsInterpolation>();
	ComponentTypeRegistry::Register<Physics::ShapeDims>();
	//========================================================

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	Graphics::DX11::GraphicsDevice graphics;
	graphics.Configure<Debug::ImGuiBackendDX11Win32>(WindowHandler::GetMainHandle(), WINDOW_WIDTH, WINDOW_HEIGHT, FPS_LIMIT);

	// デバイス & サービス（Worldコンテナ）
	Physics::PhysicsDevice::InitParams params;
	params.maxBodies = 100000;
	params.maxBodyPairs = 64 * 1024;
	params.maxContactConstraints = 2 * 1024;
	params.workerThreads = -1; // 自動

	Physics::PhysicsDevice physics;
	bool ok = physics.Initialize(params);
	if (!ok) {
		assert(false && "Failed Physics Device Initialize");
	}

	Physics::PhysicsShapeManager shapeManager;
	Physics::PhysicsService::Plan physicsPlan = { 1.0f / (float)FPS_LIMIT, 1, false };
	Physics::PhysicsService physicsService(physics, shapeManager, physicsPlan);

	Input::WinInput winInput(WindowHandler::GetMouseInput());
	InputService* inputService = &winInput;

	auto bufferMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::BufferManager>();
	Graphics::DX11::PerCamera3DService dx11PerCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I3DPerCameraService* perCameraService = &dx11PerCameraService;

	Graphics::DX11::OrtCamera3DService dx11OrtCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I3DOrtCameraService* ortCameraService = &dx11OrtCameraService;

	Graphics::DX11::Camera2DService dx112DCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I2DCameraService* camera2DService = &dx112DCameraService;

	SimpleThreadPoolService threadPool;

	auto device = graphics.GetDevice();
	auto deviceContext = graphics.GetDeviceContext();

	auto renderService = graphics.GetRenderService();

	ECS::ServiceLocator serviceLocator(renderService, &physicsService, inputService, perCameraService, ortCameraService, camera2DService, &threadPool);
	serviceLocator.InitAndRegisterStaticService<SpatialChunkRegistry>();

	Graphics::TerrainBuildParams p;
	p.cellsX = 512;
	p.cellsZ = 512;
	p.clusterCellsX = 32;
	p.clusterCellsZ = 32;
	p.cellSize = 2.5f;
	p.heightScale = 40.0f;
	p.frequency = 1.0f / 96.0f;
	p.seed = 20251030;

	std::vector<float> heightMap;
	SFW::Graphics::TerrainClustered terrain = Graphics::TerrainClustered::Build(p, &heightMap);

	std::vector<float> positions(terrain.vertices.size() * 3);
	for (auto i = 0; i < terrain.vertices.size(); ++i)
	{
		positions[i * 3 + 0] = terrain.vertices[i].pos.x;
		positions[i * 3 + 1] = terrain.vertices[i].pos.y;
		positions[i * 3 + 2] = terrain.vertices[i].pos.z;
	}
	std::vector<float> lodTarget =
	{
		1.0f, 0.25f, 0.01f
	};

	std::vector<uint32_t> outIndexPool;
	std::vector<Graphics::DX11::ClusterLodRange> outLodRanges;
	std::vector<uint32_t> outLodBase;
	std::vector<uint32_t> outLodCount;

	Graphics::DX11::GenerateClusterLODs_meshopt_fast(terrain.indexPool, terrain.clusters, positions.data(),
		terrain.vertices.size(), sizeof(Math::Vec3f), lodTarget,
		outIndexPool, outLodRanges, outLodBase, outLodCount);

	Graphics::DX11::BlockReservedContext blockRevert;
	blockRevert.Init(graphics.GetDevice(),
		L"assets/shader/CS_TerrainClustered.cso",
		L"assets/shader/CS_WriteArgs.cso",
		L"assets/shader/VS_TerrainClustered.cso",
		L"assets/shader/PS_TerrainClustered.cso",
		/*maxVisibleIndices*/ (UINT)terrain.indexPool.size());

	blockRevert.BuildLodSrvs(graphics.GetDevice(), outLodRanges, outLodBase, outLodCount);

	terrain.indexPool = std::move(outIndexPool);

	Graphics::DX11::BuildFromTerrainClustered(graphics.GetDevice(), terrain, blockRevert);

	// ---- マッピング設定 ----
	Graphics::HeightTexMapping map = Graphics::MakeHeightTexMappingFromTerrainParams(p, heightMap);

	auto& textureManager = *renderService->GetResourceManager<Graphics::DX11::TextureManager>();

	Graphics::DX11::CommonMaterialResources matRes;
	const uint32_t matIds[4] = { Mat_Grass, Mat_Rock, Mat_Dirt, Mat_Snow }; // ← あなたの素材ID
	Graphics::DX11::BuildCommonMaterialSRVs(device, textureManager, matIds, &ResolveTexturePath, matRes);

	// 0) “シート画像” の ID（例: Tex_Splat_Sheet0）は ResolveTexturePathFn でパスに解決される想定
	uint32_t sheetTexId = Tex_Splat_Control_0;

	// 1) シートを分割して各クラスタの TextureHandle を生成
	auto handles = Graphics::DX11::BuildClusterSplatTexturesFromSingleSheet(
		device, deviceContext, textureManager,
		terrain.clustersX, terrain.clustersZ,
		sheetTexId, &ResolveTexturePath,
		/*sheetIsSRGB=*/false // 重みなので通常は false
	);

	// 2) 生成ハンドルにアプリ側の “ID” を割当て → terrain.splat[cid].splatTextureId に反映
	auto AllocateSplatId = [&](Graphics::TextureHandle h, uint32_t cx, uint32_t cz, uint32_t cid)->uint32_t {
		// 例: ハンドルのインデックス等で安定ハッシュを作る or 登録テーブルに入れて返す
		//     必要ならここで "ID→TextureHandle" の辞書も作っておく（レンダラー層で参照）
		return (0x70000000u + cid); // 例：単純に cid ベースの ID
		};
	Graphics::DX11::AssignClusterSplatsFromHandles(terrain, terrain.clustersX, terrain.clustersZ, handles,
		+[](Graphics::TextureHandle h, uint32_t cx, uint32_t cz, uint32_t cid) { return (0x70000000u + cid); },
		/*queryLayer*/nullptr);

	Graphics::DX11::SplatArrayResources splatRes;
	Graphics::DX11::InitSplatArrayResources(device, splatRes, terrain.clusters.size());

	// Array 構築（CopySubresourceRegion 実行）
	std::vector<uint32_t> uniqueIds;
	BuildClusterSplatArrayResources(device, deviceContext, textureManager, terrain, &ResolveTexturePath, splatRes, &uniqueIds);

	// スライステーブルは Array 生成時の uniqueIds から得る
	std::unordered_map<uint32_t, int> id2slice = Graphics::DX11::BuildSliceTable(uniqueIds);

	// CPU配列に詰める
	Graphics::DX11::ClusterParamsGPU cp{};
	Graphics::DX11::FillClusterParamsCPU(terrain, id2slice, cp);

	// グリッドCBを設定（TerrainClustered の定義に合わせて）
	Graphics::DX11::SetupTerrainGridCB(/*originXZ=*/{ 0, 0 },
		/*cellSize=*/{ p.cellSize, p.cellSize },
		/*dimX=*/p.cellsX, /*dimZ=*/p.cellsZ, cp);

	// GPUリソースを作る/更新
	Graphics::DX11::BuildOrUpdateClusterParamsSB(device, deviceContext, cp);
	Graphics::DX11::BuildOrUpdateTerrainGridCB(device, deviceContext, cp);

	auto terrainUpdateFunc = [map, perCameraService, &terrain](Graphics::RenderService* renderService)
		{
			auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			static Graphics::DefaultLodSelector lodSel = {};

			// ---- 高さメッシュ（粗）オプション ----
			Graphics::HeightCoarseOptions2 hopt{};
			hopt.upDotMin = 0.65f;
			hopt.maxSlopeTan = 0.0f; // 垂直近い面は除外
			hopt.heightClampMin = -4000.f;
			hopt.heightClampMax = +8000.f;
			// 自動LOD（セル解像度）
			hopt.gridLod.minCells = 2;  //クラスター内の最小セル数
			hopt.gridLod.maxCells = 8; //クラスター内の最大セル数
			hopt.gridLod.targetCellPx = 128.f;
			// 高さバイアス
			hopt.bias.baseDown = 0.05f;  // 常に5cm下げる
			hopt.bias.slopeK = 0.20f;  // 斜面で追加ダウン

			// ---- 画面占有率・LOD 等 ----
			Graphics::OccluderExtractOptions opt{};
			opt.viewProj = viewProj.data();
			opt.viewportW = width;
			opt.viewportH = height;
			opt.cameraPos = camPos;
			opt.minAreaPx = 2000.f;
			opt.maxClusters = 48;
			opt.backfaceCull = true;

			std::vector<uint32_t> clusterIds;
			std::vector<Graphics::SoftTriWorld> trisW;
			std::vector<Graphics::SoftTriClip>  trisC;

			// ---- ハイブリッド抽出 ----
			ExtractOccluderTriangles_HeightmapCoarse_Hybrid(
				terrain, map, hopt, opt, clusterIds, trisW, &trisC);

			// MOCバインディング
			auto MyMOCRender = [renderService](const float* packedXYZW, uint32_t vertexCount,
				const uint32_t* indices, uint32_t indexCount,
				uint32_t vpW, uint32_t vpH)
				{
					Graphics::MocTriBatch tris =
					{
						packedXYZW,			//const float* clipVertices = nullptr; // (x, y, z, w) 配列
						indices,			//const uint32_t* indices = nullptr;   // インデックス配列
						vertexCount / 3,	//uint32_t      numTriangles = 0;
						true				//bool          valid = true;          // 近クリップ全面裏などなら false
					};

					renderService->RenderingOccluderInMOC(tris);
				};

			// MOCにオクル―ダーを描画
			Graphics::DispatchToMOC(MyMOCRender, trisC, width, height);
		};

	auto testClusterFunc = [&graphics, deviceContext, perCameraService, matRes, splatRes, cp, &blockRevert](Graphics::RenderService* renderService)
		{
			auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();
			Math::Frustumf frustumPlanes
				//Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
				= perCameraService->MakeFrustum(true);

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			graphics.SetDefaultRenderTarget();
			graphics.SetRasterizerState(Graphics::RasterizerStateID::WireCullNone);

			// フレームの先頭 or Terrainパスの先頭で 1回だけ：
			Graphics::DX11::BindCommonMaterials(deviceContext, matRes);

			ID3D11ShaderResourceView* splatSrv = splatRes.splatArraySRV.Get();
			deviceContext->PSSetShaderResources(14, 1, &splatSrv);       // t14
			Graphics::DX11::BindClusterParamsForOneCall(deviceContext, cp);              // t15, b2

			auto world = Math::Matrix4x4f::Identity();
			blockRevert.Run(deviceContext, frustumPlanes.data(), viewProj.data(), world.data(), width, height, 400.0f, 160.0f);
		};

	renderService->SetCustomUpdateFunction(std::move(terrainUpdateFunc));
	renderService->SetCustomPreDrawFunction(std::move(testClusterFunc));

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SFW::Graphics;

	graphics.TestInitialize();
	auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11::ShaderManager>();
	DX11::ShaderCreateDesc shaderDesc;
	shaderDesc.templateID = MaterialTemplateID::PBR;
	shaderDesc.vsPath = L"assets/shader/VS_Default.cso";
	shaderDesc.psPath = L"assets/shader/PS_Default.cso";
	ShaderHandle shaderHandle;
	shaderMgr->Add(shaderDesc, shaderHandle);

	DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
	auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11::PSOManager>();
	PSOHandle psoHandle;
	psoMgr->Add(psoDesc, psoHandle);

	ModelAssetHandle modelAssetHandle[4];

	auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11::ModelAssetManager>();
	// モデルアセットの読み込み
	DX11::ModelAssetCreateDesc modelDesc;
	modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_1.gltf";
	modelDesc.pso = psoHandle;
	modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
	modelDesc.instancesPeak = 1000;
	modelDesc.viewMax = 1000.0f;

	modelAssetMgr->Add(modelDesc, modelAssetHandle[0]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/Clover_1.gltf";
	modelAssetMgr->Add(modelDesc, modelAssetHandle[1]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/DeadTree_1.gltf";
	modelDesc.buildOccluders = false;
	modelAssetMgr->Add(modelDesc, modelAssetHandle[2]);

	modelDesc.path = "assets/model/StylizedNatureMegaKit/Grass_Common_Tall.gltf";
	modelAssetMgr->Add(modelDesc, modelAssetHandle[3]);


	//========================================================================================-

	World<Grid2DPartition, Grid3DPartition, QuadTreePartition, OctreePartition> world(std::move(serviceLocator));
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	std::random_device rd;
	std::mt19937_64 rng(rd());

	// 例: A=50%, B=30%, C=20% のつもりで重みを設定（整数でも実数でもOK）
	std::array<int, 4> weights{ 20, 30, 10 ,50};
	std::discrete_distribution<int> dist(weights.begin(), weights.end());

	for (int i = 0; i < 1; ++i) {
		auto level = std::unique_ptr<Level<OctreePartition>>(new Level<OctreePartition>("Level" + std::to_string(i), *entityManagerReg, ELevelState::Main));

		// System登録
		auto& scheduler = level->GetScheduler();

		scheduler.AddSystem<ModelRenderSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<SimpleModelRenderSystem>(world.GetServiceLocator());
		scheduler.AddSystem<CameraSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<TestMoveSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<PhysicsSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<BuildBodiesFromIntentsSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<DebugRenderSystem>(world.GetServiceLocator());
		//scheduler.AddSystem<CleanModelSystem>(world.GetServiceLocator());

		auto ps = world.GetServiceLocator().Get<Physics::PhysicsService>();
		auto sphere = ps->MakeSphere(0.5f);//ps->MakeBox({ 0.5f, 0.5f, 0.5f }); // Box形状を生成
		auto sphereDims = ps->GetShapeDims(sphere);

		auto box = ps->MakeBox({ 1000.0f,0.5f, 1000.0f });
		auto boxDims = ps->GetShapeDims(box);

		Math::Vec3f src = { 0.0f,50.0f,0.0f };
		Math::Vec3f dst = src;

		// Entity生成
		for (int j = 0; j < 1; ++j) {
			for (int k = 0; k < 1; ++k) {
				for (int n = 0; n < 1; ++n) {
					Math::Vec3f location = { float(rand() % 3000 + 1), float(n) * 20.0f, float(rand() % 3000 + 1) };
					//Math::Vec3f location = { 10.0f * j,0.0f,10.0f * k };
					auto chunk = level->GetChunk(location);
					auto key = chunk.value()->GetNodeKey();
					SpatialMotionTag tag{};
					tag.handle = { key, chunk.value() };
					float scale = 1.0f + float(rand() % 100 - 50) / 100.0f;
					//float scale = 1.0f;
					auto id = level->AddEntity(
						TransformSoA{ location, Math::Quatf(0.0f,0.0f,0.0f,1.0f),Math::Vec3f(scale,scale,scale) },
						CModel{ modelAssetHandle[dist(rng)] },
						Physics::BodyComponent{},
						Physics::PhysicsInterpolation(
							location, // 初期位置
							Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
						),
						sphereDims.value(),
						tag
					);
					/*if (id) {
						ps->EnqueueCreateIntent(id.value(), sphere, key);
					}*/
				}
			}
		}

		//Physics::BodyComponent staticBody{};
		//staticBody.isStatic = Physics::BodyType::Static; // staticにする
		//auto id = level->AddEntity(
		//	TransformSoA{ 10.0f,-10.0f, 10.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
		//	CModel{ modelAssetHandle },
		//	staticBody,
		//	Physics::PhysicsInterpolation(
		//		Math::Vec3f{ 10.0f,-10.0f, 10.0f }, // 初期位置
		//		Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
		//	),
		//	boxDims.value()
		//);
		//if (id) {
		//	auto chunk = level->GetChunk({ 0.0f,-100.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
		//	ps->EnqueueCreateIntent(id.value(), box, chunk.value()->GetNodeKey());
		//}

		world.AddLevel(std::move(level));
	}

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop();
		});

	return WindowHandler::Destroy();
}