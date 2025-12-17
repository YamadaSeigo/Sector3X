
//SectorFW
#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include <SectorFW/DX11WinTerrainHelper.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>
#include <SectorFW/Graphics/DX11/DX11ShadowMapService.h>
#include <SectorFW/Graphics/TerrainOccluderExtraction.h>

//System
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/DebugRenderSystem.h"
#include "system/CleanModelSystem.h"
#include "system/SimpleModelRenderSystem.h"
#include "system/SpriteRenderSystem.h"
#include "system/PlayerSystem.h"
#include "system/EnviromentSystem.h"
#include "WindMovementService.h"

#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

#define WINDOW_NAME "SectorX Console Project"

//4の倍数
constexpr uint32_t WINDOW_WIDTH = uint32_t(1920 / 1.5f);	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = uint32_t(1080 / 1.5f);	// ウィンドウの高さ

constexpr uint32_t SHADOW_MAP_WIDTH = 1024 / 2;	// シャドウマップの幅
constexpr uint32_t SHADOW_MAP_HEIGHT = 1024 / 2;	// シャドウマップの高さ

constexpr double FPS_LIMIT = 60.0;	// フレームレート制限

#include "RenderDefine.h"

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
	{ Mat_Rock,  { "assets/texture/terrain/small+rocks+ground.jpg",  true } },
	{ Mat_Dirt,  { "assets/texture/terrain/DirtHight.png",  true } },
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

//カスタム関数を実行するかのフラグ
static std::atomic<bool> isExecuteCustomFunc = false;

struct RtPack
{
	std::vector<ComPtr<ID3D11Texture2D>>        tex;
	std::vector<ComPtr<ID3D11RenderTargetView>> rtv;
	std::vector<ComPtr<ID3D11ShaderResourceView>> srv;
};

bool CreateMRT(ID3D11Device* dev, UINT w, UINT h, RtPack& out)
{
	DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w;
	td.Height = h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = fmt;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	out.tex.resize(2);
	out.rtv.resize(2);
	out.srv.resize(2);

	for (int i = 0; i < 2; ++i)
	{
		HRESULT hr = dev->CreateTexture2D(&td, nullptr, out.tex[i].GetAddressOf());
		if (FAILED(hr)) return false;

		hr = dev->CreateRenderTargetView(out.tex[i].Get(), nullptr, out.rtv[i].GetAddressOf());
		if (FAILED(hr)) return false;

		hr = dev->CreateShaderResourceView(out.tex[i].Get(), nullptr, out.srv[i].GetAddressOf());
		if (FAILED(hr)) return false;
	}
	return true;
}


// 描画のパイプライン初期化
void InitializeRenderPipeLine(
	Graphics::DX11::GraphicsDevice::RenderGraph* renderGraph,
	Graphics::DX11::GraphicsDevice* graphics,
	ComPtr<ID3D11RenderTargetView>& mainRenderTarget,
	ComPtr<ID3D11DepthStencilView>& mainDepthStencilView,
	Graphics::PassCustomFuncType drawTerrainColor,
	Graphics::DX11::ShadowMapService shadowMapService)
{
	using namespace SFW::Graphics;

	auto bufferMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::BufferManager>();
	auto cameraHandle3D = bufferMgr->FindByName(DX11::PerCamera3DService::BUFFER_NAME);
	auto cameraHandle2D = bufferMgr->FindByName(DX11::Camera2DService::BUFFER_NAME);

	auto shaderMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::ShaderManager>();
	auto psoMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::PSOManager>();

	static RtPack ttMRT;
	CreateMRT(graphics->GetDevice(), WINDOW_WIDTH, WINDOW_HEIGHT, ttMRT);

	std::vector<ComPtr<ID3D11RenderTargetView>> mainRtv = { mainRenderTarget };

	auto& main3DGroup = renderGraph->AddPassGroup(PassGroupName[GROUP_3D_MAIN]);

	//PSを指定しないことでDepthOnlyのPSOを作成
	DX11::ShaderCreateDesc shaderDesc;
	shaderDesc.vsPath = L"assets/shader/VS_CascadeDepth.cso";

	ShaderHandle shaderHandle;
	shaderMgr->Add(shaderDesc, shaderHandle);
	DX11::PSOCreateDesc psoDesc;
	psoDesc.shader = shaderHandle;
	PSOHandle psoHandle;
	psoMgr->Add(psoDesc, psoHandle);

	RenderPassDesc<ID3D11RenderTargetView*, ID3D11DepthStencilView*, ComPtr> passDesc;
	passDesc.blendState = BlendStateID::Opaque;
	passDesc.psoOverride = psoHandle;

	struct CascadeIndex {
		UINT index;
		UINT padding[3];
	};

	const auto& cascadeDSVs = shadowMapService.GetCascadeDSV();

	auto cascadeCount = cascadeDSVs.size();

	DX11::BufferCreateDesc cbDesc;
	cbDesc.size = sizeof(CascadeIndex);
	for (UINT i = 0; i < cascadeCount; ++i)
	{
		cbDesc.name = "CascadeIndexCB_" + std::to_string(i);
		CascadeIndex data;
		data.index = i;

		cbDesc.initialData = &data;
		BufferHandle cascadeIndexHandle;
		bufferMgr->Add(cbDesc, cascadeIndexHandle);
		passDesc.cbvs = { BindSlotBuffer{13, cascadeIndexHandle} };

		passDesc.dsv = cascadeDSVs[i];

		if (i == 0)
		{
			Viewport vp;
			vp.width = (float)SHADOW_MAP_WIDTH;
			vp.height = (float)SHADOW_MAP_HEIGHT;

			passDesc.viewport = vp;
		}
		else
		{
			passDesc.viewport = std::nullopt;
		}

		renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_CASCADE0 << i);
	}

	shaderDesc.vsPath = L"assets/shader/VS_ZPrepass.cso";

	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoMgr->Add(psoDesc, psoHandle);

	Viewport vp;
	vp.width = (float)WINDOW_WIDTH;
	vp.height = (float)WINDOW_HEIGHT;
	passDesc.viewport = vp;

	passDesc.dsv = mainDepthStencilView;
	passDesc.cbvs = { BindSlotBuffer{cameraHandle3D} };
	passDesc.psoOverride = psoHandle;
	passDesc.customExecute = drawTerrainColor;

	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_ZPREPASS);

	shaderDesc.vsPath = L"assets/shader/VS_Sky.cso";
	shaderDesc.psPath = L"assets/shader/PS_Sky.cso";
	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoDesc.rasterizerState = RasterizerStateID::SolidCullNone;
	psoMgr->Add(psoDesc, psoHandle);

	DX11::ModelAssetCreateDesc modelDesc;
	modelDesc.path = "assets/model/SkyStars.gltf";
	modelDesc.pso = psoHandle;
	modelDesc.rhFlipZ = true;
	ModelAssetHandle skyboxModelHandle;
	auto modelMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::ModelAssetManager>();
	modelMgr->Add(modelDesc, skyboxModelHandle);

	auto modelData = modelMgr->Get(skyboxModelHandle);
	static MeshHandle skyboxMeshHandle = modelData.ref().subMeshes[0].lods[0].mesh;
	static MaterialHandle skyboxMaterialHandle = modelData.ref().subMeshes[0].material;
	static PSOHandle skyboxPsoHandle = psoHandle;

	//本当はやるべきではないが
	//graphicsがstaticであることを前提にしてラムダ式でキャプチャなしでアクセスするために所有権保持
	static auto gGraphics = graphics;
	static auto renderBackend = graphics->GetBackend();

	struct SkyCB {
		float time;
		float rotateSpeed;
		float padding[2];
	};

	static SkyCB skyboxData = { 0.0f,0.005f,0.0f,0.0f };

	DX11::BufferCreateDesc cbSkyboxDesc;
	cbSkyboxDesc.name = "SkyboxCB";
	cbSkyboxDesc.size = sizeof(SkyCB);
	cbSkyboxDesc.initialData = &skyboxData;
	BufferHandle skyboxCBHandle = {};
	auto skyBufData = bufferMgr->CreateResource(cbSkyboxDesc, skyboxCBHandle);
	static ComPtr<ID3D11Buffer> SkyCBBuffer = skyBufData.buffer;

	auto skyboxDraw = [](uint64_t frame) {
		bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
		if (!execute) return;

		skyboxData.time += (float)(1.0f / FPS_LIMIT);

		renderBackend->UpdateBufferDataImpl(SkyCBBuffer.Get(), &skyboxData, sizeof(skyboxData));

		gGraphics->SetDepthStencilState(DepthStencilStateID::DepthReadOnly);
		renderBackend->BindPSCBVs({ SkyCBBuffer.Get()}, 12);
		renderBackend->DrawInstanced(skyboxMeshHandle.index, skyboxMaterialHandle.index, skyboxPsoHandle.index, 1, true);
		};

	auto calcDeffered = []() {
		auto ctx = gGraphics->GetDeviceContext();

		};

	passDesc.rtvs = ttMRT.rtv;
	passDesc.dsv = mainDepthStencilView;
	passDesc.cbvs = { BindSlotBuffer{cameraHandle3D} };
	passDesc.psoOverride = std::nullopt;
	passDesc.viewport = vp;
	passDesc.depthStencilState = DepthStencilStateID::Default_Stencil;
	passDesc.customExecute = nullptr;
	passDesc.stencilRef = 1;
	//passDesc.rasterizerState = RasterizerStateID::WireCullNone;

	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_OUTLINE);

	passDesc.customExecute = skyboxDraw;
	passDesc.stencilRef = 2;
	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_OPAQUE);

	shaderDesc.vsPath = L"assets/shader/VS_Unlit.cso";
	shaderDesc.psPath = L"assets/shader/PS_HighLight.cso";
	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoDesc.rasterizerState = RasterizerStateID::SolidCullBack;
	psoMgr->Add(psoDesc, psoHandle);

	passDesc.customExecute = nullptr;
	passDesc.psoOverride = psoHandle;
	passDesc.blendState = BlendStateID::Opaque;
	passDesc.depthStencilState = DepthStencilStateID::DepthReadOnly_Greater_Read_Stencil;
	passDesc.stencilRef = 1;
	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_HIGHLIGHT);

	auto& UIGroup = renderGraph->AddPassGroup(PassGroupName[GROUP_UI]);

	passDesc.rtvs = mainRtv;
	passDesc.viewport = std::nullopt;
	passDesc.customExecute = nullptr;

	passDesc.topology = PrimitiveTopology::LineList;
	passDesc.rasterizerState = RasterizerStateID::WireCullNone;
	passDesc.blendState = BlendStateID::Opaque;
	passDesc.psoOverride = std::nullopt;
	passDesc.depthStencilState = DepthStencilStateID::DepthReadOnly;

	renderGraph->AddPassToGroup(UIGroup, passDesc, PASS_UI_3DLINE);

	passDesc.dsv = nullptr;
	passDesc.cbvs = { cameraHandle2D };
	passDesc.topology = PrimitiveTopology::TriangleList;
	passDesc.rasterizerState = std::nullopt;
	passDesc.blendState = BlendStateID::AlphaBlend;

	renderGraph->AddPassToGroup(UIGroup, passDesc, PASS_UI_MAIN);

	passDesc.topology = PrimitiveTopology::LineList;
	passDesc.rasterizerState = RasterizerStateID::WireCullNone;
	passDesc.blendState = BlendStateID::Opaque;

	renderGraph->AddPassToGroup(UIGroup, passDesc, PASS_UI_LINE);


	//グループとパスの実行順序を設定(現状は登録した順番のインデックスで指定)
	std::vector<DX11::GraphicsDevice::RenderGraph::PassNode> order = {
		{ 0, 0 },
		{ 0, 1 },
		{ 0, 2 },
		{ 0, 3 },
		{ 0, 4 },
		{ 0, 5 },
		{ 0, 6 },
		{ 1, 0 },
		{ 1, 1 },
		{ 1, 2 }
	};

	renderGraph->SetExecutionOrder(order);
}

int main(void)
{
	LOG_INFO("SectorX Console Project started");

	//==コンポーネントの登録===================================================
	//main.cppに集めた方がコンパイル効率がいいので、ここで登録している
	//※複数人で開発する場合は、各自のコンポーネントを別ファイルに分けて登録するようにする
	ComponentTypeRegistry::Register<CModel>();
	ComponentTypeRegistry::Register<TransformSoA>();
	ComponentTypeRegistry::Register<SpatialMotionTag>();
	ComponentTypeRegistry::Register<Physics::BodyComponent>();
	ComponentTypeRegistry::Register<Physics::PhysicsInterpolation>();
	ComponentTypeRegistry::Register<Physics::ShapeDims>();
	ComponentTypeRegistry::Register<CSprite>();
	//======================================================================

	// ウィンドウの作成
	WindowHandler::Create(_T(WINDOW_NAME), WINDOW_WIDTH, WINDOW_HEIGHT);

	static Graphics::DX11::GraphicsDevice graphics;
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
	static Graphics::I3DPerCameraService* perCameraService = &dx11PerCameraService;

	Graphics::DX11::OrtCamera3DService dx11OrtCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I3DOrtCameraService* ortCameraService = &dx11OrtCameraService;

	Graphics::DX11::Camera2DService dx112DCameraService(bufferMgr, WINDOW_WIDTH, WINDOW_HEIGHT);
	Graphics::I2DCameraService* camera2DService = &dx112DCameraService;

	auto device = graphics.GetDevice();
	auto deviceContext = graphics.GetDeviceContext();

	auto renderService = graphics.GetRenderService();

	static Graphics::LightShadowService lightShadowService;
	Graphics::LightShadowService::CascadeConfig cascadeConfig;
	cascadeConfig.shadowMapResolution = Math::Vec2f(float(SHADOW_MAP_WIDTH), float(SHADOW_MAP_HEIGHT));
	cascadeConfig.shadowDistance = 40.0f;
	lightShadowService.SetCascadeConfig(cascadeConfig);

	WindMovementService grassService(bufferMgr);

	PlayerService playerService(bufferMgr);

	Audio::AudioService audioService;
	audioService.Initialize();

	ECS::ServiceLocator serviceLocator(renderService, &physicsService, inputService, perCameraService,
		ortCameraService, camera2DService, &lightShadowService, &grassService, &playerService, &audioService);
	serviceLocator.InitAndRegisterStaticService<SpatialChunkRegistry>();

	Graphics::TerrainBuildParams p;
	p.cellsX = 512 * 1;
	p.cellsZ = 512 * 1;
	p.clusterCellsX = 16;
	p.clusterCellsZ = 16;
	p.cellSize = 3.0f;
	p.heightScale = 80.0f;
	p.frequency = 1.0f / 96.0f * 1.0f;
	p.seed = 20251212;
	p.offset.y -= 40.0f;

	std::vector<float> heightMap;
	static SFW::Graphics::TerrainClustered terrain = Graphics::TerrainClustered::Build(p, &heightMap);

	std::vector<float> positions(terrain.vertices.size() * 3);
	auto vertexSize = terrain.vertices.size();
	for (auto i = 0; i < vertexSize; ++i)
	{
		positions[i * 3 + 0] = terrain.vertices[i].pos.x;
		positions[i * 3 + 1] = terrain.vertices[i].pos.y;
		positions[i * 3 + 2] = terrain.vertices[i].pos.z;
	}
	std::vector<float> lodTarget =
	{
		1.0f, 0.25f, 0.075f
	};

	std::vector<uint32_t> outIndexPool;
	std::vector<Graphics::DX11::ClusterLodRange> outLodRanges;
	std::vector<uint32_t> outLodBase;
	std::vector<uint32_t> outLodCount;


	bool check = Graphics::TerrainClustered::CheckClusterBorderEquality(terrain.indexPool, terrain.clusters, terrain.clustersX, terrain.clustersZ);

	//Graphics::TerrainClustered::WeldVerticesAlongBorders(terrain.vertices, terrain.indexPool, p.cellSize);

	//Graphics::TerrainClustered::AddSkirtsToClusters(terrain, /*skirtDepth=*/100.0f);

	Graphics::DX11::GenerateClusterLODs_meshopt_fast(terrain.indexPool, terrain.clusters, positions.data(),
		terrain.vertices.size(), sizeof(Math::Vec3f), lodTarget,
		outIndexPool, outLodRanges, outLodBase, outLodCount);

	static Graphics::DX11::BlockReservedContext blockRevert;
	ok = blockRevert.Init(graphics.GetDevice(),
		L"assets/shader/CS_TerrainClustered.cso",
		L"assets/shader/CS_TerrainClusteredShadow.cso",
		L"assets/shader/CS_WriteArgs.cso",
		L"assets/shader/CS_WriteArgsShadow.cso",
		L"assets/shader/VS_TerrainClustered.cso",
		L"assets/shader/VS_TerrainClusteredDepth.cso",
		L"assets/shader/PS_TerrainClustered.cso",
		/*maxVisibleIndices*/ (UINT)terrain.indexPool.size());

	assert(ok && "Failed BlockRevert Init");

	blockRevert.BuildLodSrvs(graphics.GetDevice(), outLodRanges, outLodBase, outLodCount);

	terrain.indexPool = std::move(outIndexPool);

	Graphics::DX11::BuildFromTerrainClustered(graphics.GetDevice(), terrain, blockRevert);

	// ---- マッピング設定 ----
	static Graphics::HeightTexMapping map = Graphics::MakeHeightTexMappingFromTerrainParams(p, heightMap);

	auto& textureManager = *renderService->GetResourceManager<Graphics::DX11::TextureManager>();

	static Graphics::DX11::CommonMaterialResources matRes;
	const uint32_t matIds[4] = { Mat_Grass, Mat_Rock, Mat_Dirt, Mat_Snow }; // ← あなたの素材ID
	Graphics::DX11::BuildCommonMaterialSRVs(device, textureManager, matIds, &ResolveTexturePath, matRes);

	// 0) “シート画像” の ID（例: Tex_Splat_Sheet0）は ResolveTexturePathFn でパスに解決される想定
	uint32_t sheetTexId = Tex_Splat_Control_0;

	ComPtr<ID3D11Texture2D> sheetTex;
	// 1) シートを分割して各クラスタの TextureHandle を生成
	auto handles = Graphics::DX11::BuildClusterSplatTexturesFromSingleSheet(
		device, deviceContext, textureManager,
		sheetTex,
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
		[](Graphics::TextureHandle h, uint32_t cx, uint32_t cz, uint32_t cid) { return (0x70000000u + cid); },
		/*queryLayer*/nullptr,
		//サンプリングでクラスターの境界に線が発生する問題をとりあえずScaleとOffsetで解決
		{ 0.985f, 0.985f }, { 0.0015f, 0.0015f });

	static Graphics::DX11::SplatArrayResources splatRes;
	Graphics::DX11::InitSplatArrayResources(device, splatRes, terrain.clusters.size());

	BuildSplatArrayFromHandles(device, deviceContext, textureManager, handles, splatRes);

	// スライステーブルは Array 生成時の uniqueIds から得る
	std::vector<uint32_t> uniqueIds; Graphics::DX11::CollectUniqueSplatIds(terrain, uniqueIds);
	std::unordered_map<uint32_t, int> id2slice = Graphics::DX11::BuildSliceTable(uniqueIds);

	// CPU配列に詰める
	static Graphics::DX11::ClusterParamsGPU cp{};
	Graphics::DX11::FillClusterParamsCPU(terrain, id2slice, cp);

	// グリッドCBを設定（TerrainClustered の定義に合わせて）
	Graphics::DX11::SetupTerrainGridCB(/*originXZ=*/{ 0, 0 },
		/*cellSize=*/{ p.clusterCellsX * p.cellSize, p.clusterCellsZ * p.cellSize },
		/*dimX=*/terrain.clustersX, /*dimZ=*/terrain.clustersZ, cp);

	// 地形のクラスター専用GPUリソースを作る/初期更新
	Graphics::DX11::BuildOrUpdateClusterParamsSB(device, deviceContext, cp);
	Graphics::DX11::BuildOrUpdateTerrainGridCB(device, deviceContext, cp);

	static Graphics::DX11::ShadowMapService shadowMapService;
	Graphics::DX11::ShadowMapConfig shadowMapConfig;
	shadowMapConfig.width = SHADOW_MAP_WIDTH;
	shadowMapConfig.height = SHADOW_MAP_HEIGHT;
	ok = shadowMapService.Initialize(device, shadowMapConfig);
	assert(ok && "Failed ShadowMapService Initialize");

	auto terrainUpdateFunc = [](Graphics::RenderService* renderService)
		{
			bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
			if (!execute) return;

			auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			static Graphics::DefaultLodSelector lodSel = {};

			// ---- 高さメッシュ（粗）オプション ----
			Graphics::HeightCoarseOptions2 hopt{};
			hopt.upDotMin = 0.65f;
			hopt.maxSlopeTan = 5.0f; // 垂直近い面は除外
			hopt.heightClampMin = -4000.f;
			hopt.heightClampMax = +8000.f;
			// 自動LOD（セル解像度）
			hopt.gridLod.minCells = 2;  //クラスター内の最小セル数
			hopt.gridLod.maxCells = 8; //クラスター内の最大セル数
			hopt.gridLod.targetCellPx = 128.f;
			// 高さバイアス
			hopt.bias.baseDown = 0.05f;  // 常に5cm下げる
			hopt.bias.slopeK = 0.00f;  // 斜面で追加ダウン

			// ---- 画面占有率・LOD 等 ----
			Graphics::OccluderExtractOptions opt{};
			opt.viewProj = viewProj.data();
			opt.viewportW = width;
			opt.viewportH = height;
			opt.cameraPos = camPos;
			opt.minAreaPx = 2000.f;
			opt.maxClusters = 64;
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

	auto ClusterDrawDepthFunc = [](Graphics::RenderService* renderService)
		{
			bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
			if (!execute) return;

			//auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();
			Math::Frustumf frustumPlanes
				//Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
				= perCameraService->MakeFrustum(true);

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::Default);

			static Graphics::DX11::BlockReservedContext::ShadowDepthParams shadowParams{};

			shadowParams.mainDSV = graphics.GetMainDepthStencilView().Get();
			shadowParams.mainViewProj = perCameraService->MakeViewProjMatrix();
			memcpy(shadowParams.mainFrustumPlanes, frustumPlanes.data(), sizeof(shadowParams.mainFrustumPlanes));
			auto& cascadeDSV = shadowMapService.GetCascadeDSV();
			for (int c = 0; c < Graphics::kMaxShadowCascades; ++c) {
				shadowParams.cascadeDSV[c] = cascadeDSV[c].Get();
			}
			auto& cascade = lightShadowService.GetCascades();
			memcpy(shadowParams.lightViewProj, cascade.lightViewProj.data(), sizeof(shadowParams.lightViewProj));
			memcpy(shadowParams.cascadeFrustumPlanes, cascade.frustumWS.data(), sizeof(shadowParams.cascadeFrustumPlanes));

			shadowParams.screenW = WINDOW_WIDTH;
			shadowParams.screenH = WINDOW_HEIGHT;

			auto deviceContext = graphics.GetDeviceContext();

			// シャドウマップ用のCB/SRV/サンプラーを解除
			constexpr ID3D11Buffer* nullBuffer = nullptr;
			deviceContext->PSSetConstantBuffers(5, 1, &nullBuffer);
			constexpr ID3D11ShaderResourceView* nullSRV = nullptr;
			deviceContext->PSSetShaderResources(7, 1, &nullSRV);
			constexpr ID3D11SamplerState* nullSampler = nullptr;
			deviceContext->PSSetSamplers(1, 1, &nullSampler);

			shadowMapService.ClearDepthBuffer(deviceContext);

			//CBの5, Samplerの1にバインド
			shadowMapService.BindShadowResources(deviceContext, 5, 1);

			auto bufMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
			auto cameraHandle = bufMgr->FindByName(Graphics::DX11::PerCamera3DService::BUFFER_NAME);
			auto cameraBufData = bufMgr->Get(cameraHandle);

			blockRevert.RunShadowDepth(deviceContext, cameraBufData->buffer,
				shadowParams, &shadowMapService.GetCascadeViewport());
		};

	auto drawTerrainColor = [](uint64_t frame)
		{
			bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
			if (!execute) return;

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);
			graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

			auto deviceContext = graphics.GetDeviceContext();

			// フレームの先頭 or Terrainパスの先頭で 1回だけ：
			Graphics::DX11::BindCommonMaterials(deviceContext, matRes);

			ID3D11ShaderResourceView* splatSrv = splatRes.splatArraySRV.Get();
			deviceContext->PSSetShaderResources(24, 1, &splatSrv);       // t24
			Graphics::DX11::BindClusterParamsForOneCall(deviceContext, cp);              // t25, b10

			//書き込みと読み込みを両立させないために、デフォルトのレンダーターゲットに戻す
			graphics.SetDefaultRenderTarget();

			deviceContext->RSSetViewports(1, &graphics.GetMainViewport());

			shadowMapService.BindShadowPSShadowMap(deviceContext, 7);

			shadowMapService.UpdateShadowCascadeCB(deviceContext, lightShadowService);

			shadowMapService.BindShadowRasterizer(deviceContext);

			blockRevert.RunColor(deviceContext,
				graphics.GetMainRenderTargetView().Get(),
				graphics.GetMainDepthStencilView().Get()
			);
		};

	renderService->SetCustomUpdateFunction(terrainUpdateFunc);
	renderService->SetCustomPreDrawFunction(ClusterDrawDepthFunc);

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SFW::Graphics;

	// レンダーパイプライン初期化関数
	graphics.ExecuteCustomFunc([&](
		Graphics::DX11::GraphicsDevice::RenderGraph* renderGraph,
		ComPtr<ID3D11RenderTargetView>& mainRenderTarget,
		ComPtr<ID3D11DepthStencilView>& mainDepthStencilView)
		{
			InitializeRenderPipeLine(renderGraph, &graphics, mainRenderTarget, mainDepthStencilView, drawTerrainColor, shadowMapService);
		}
	);


	Graphics::DX11::CpuImage cpuSplatImage;
	Graphics::DX11::ReadTexture2DToCPU(device, deviceContext, sheetTex.Get(), cpuSplatImage);

	SFW::World<Grid2DPartition, QuadTreePartition, VoidPartition> world(std::move(serviceLocator));
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	{
		auto level = std::unique_ptr<Level<VoidPartition>>(new Level<VoidPartition>("Title", *entityManagerReg, ELevelState::Main));

		auto worldSession = world.GetSession();
		worldSession.AddLevel(std::move(level),
			[&](std::add_pointer_t<decltype(world)> pWorld, SFW::Level<VoidPartition>* pLevel)
			{
				auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
				auto matMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::MaterialManager>();
				auto shaderMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::ShaderManager>();
				auto sampMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::SamplerManager>();


				DX11::ShaderCreateDesc shaderDesc;
				shaderDesc.vsPath = L"assets/shader/VS_Unlit.cso";
				shaderDesc.psPath = L"assets/shader/PS_Color.cso";
				ShaderHandle shaderHandle;
				shaderMgr->Add(shaderDesc, shaderHandle);

				D3D11_SAMPLER_DESC sampDesc = {};
				sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
				SamplerHandle samp = sampMgr->AddWithDesc(sampDesc);

				DX11::TextureCreateDesc textureDesc;
				textureDesc.path = "assets/texture/sprite/TitleText.png";
				textureDesc.forceSRGB = true;
				Graphics::TextureHandle texHandle;
				textureMgr->Add(textureDesc, texHandle);
				Graphics::DX11::MaterialCreateDesc matDesc;

				auto windCBHandle = grassService.GetBufferHandle();

				matDesc.shader = shaderHandle;
				matDesc.samplerMap[0] = samp;
				matDesc.vsCBV[11] = windCBHandle; // VS_CB11 にセット
				matDesc.psSRV[2] = texHandle; // TEX2 にセット

				Graphics::MaterialHandle matHandle;
				matMgr->Add(matDesc, matHandle);
				CSprite sprite;
				sprite.hMat = matHandle;
				auto levelSession = pLevel->GetSession();

				auto getPos = [](float x, float y)->Math::Vec3f {
					Math::Vec3f pos;
					pos.x = (WINDOW_WIDTH * x) / 2.0f;
					pos.y = (WINDOW_HEIGHT * y) / 2.0f;
					pos.z = 0.0f;
					return pos;
					};

				auto getScale = [](float x, float y)->Math::Vec3f {
					Math::Vec3f scale;
					scale.x = WINDOW_WIDTH * x;
					scale.y = WINDOW_HEIGHT * y;
					scale.z = 1.0f;
					return scale;
					};

				levelSession.AddGlobalEntity(
					CTransform{ getPos(0.0f,0.4f),{0.0f,0.0f,0.0f,1.0f}, getScale(0.7f,0.7f) },
					sprite);

				textureDesc.path = "assets/texture/sprite/PressEnter.png";
				textureMgr->Add(textureDesc, texHandle);
				matDesc.psSRV[2] = texHandle; // TEX2 にセット
				matMgr->Add(matDesc, matHandle);
				sprite.hMat = matHandle;

				levelSession.AddGlobalEntity(
					CTransform{ getPos(0.0f,-0.7f),{0.0f,0.0f,0.0f,1.0f}, getScale(0.25f,0.25f) },
					sprite);

				auto& serviceLocator = pWorld->GetServiceLocator();

				auto& scheduler = pLevel->GetScheduler();
				scheduler.AddSystem<SpriteRenderSystem>(serviceLocator);

				perCameraService->SetTarget({ 100.0f,-1.0f,100.0f });
				Math::Quatf cameraRot = Math::Quatf::FromAxisAngle({ 1.0f,0.0f,0.0f }, Math::Deg2Rad(-20.0f));
				perCameraService->Rotate(cameraRot);

			});
	}

	{
		using OpenFieldLevel = SFW::Level<Grid2DPartition>;

		auto level = std::unique_ptr<OpenFieldLevel>(new OpenFieldLevel("OpenField", *entityManagerReg, ELevelState::Main));

		auto worldSession = world.GetSession();
		worldSession.AddLevel(
			std::move(level),
			//ロード時
			[&](std::add_pointer_t<decltype(world)> pWorld, OpenFieldLevel* pLevel) {

				//地形の処理を開始
				isExecuteCustomFunc.store(true, std::memory_order_relaxed);

				auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11::ModelAssetManager>();

				auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11::ShaderManager>();

				//デフォルト描画のPSO生成
				DX11::ShaderCreateDesc shaderDesc;
				shaderDesc.templateID = MaterialTemplateID::PBR;
				shaderDesc.vsPath = L"assets/shader/VS_Unlit.cso";
				shaderDesc.psPath = L"assets/shader/PS_Unlit.cso";
				ShaderHandle shaderHandle;
				shaderMgr->Add(shaderDesc, shaderHandle);

				auto psoMgr = graphics.GetRenderService()->GetResourceManager<DX11::PSOManager>();
				DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
				PSOHandle cullDefaultPSOHandle;
				psoMgr->Add(psoDesc, cullDefaultPSOHandle);

				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
				PSOHandle cullNonePSOHandle;
				psoMgr->Add(psoDesc, cullNonePSOHandle);

				//草の揺れ用PSO生成
				shaderDesc.vsPath = L"assets/shader/VS_WindGrass.cso";
				shaderDesc.psPath = L"assets/shader/PS_ShadowColor.cso";
				shaderMgr->Add(shaderDesc, shaderHandle);
				PSOHandle windGrassPSOHandle;
				psoDesc.shader = shaderHandle;
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
				psoMgr->Add(psoDesc, windGrassPSOHandle);
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

				shaderDesc.vsPath = L"assets/shader/VS_WindEntityUnlit.cso";
				shaderDesc.psPath = L"assets/shader/PS_Unlit.cso";
				shaderMgr->Add(shaderDesc, shaderHandle);
				PSOHandle cullNoneWindEntityPSOHandle;
				psoDesc.shader = shaderHandle;
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
				psoMgr->Add(psoDesc, cullNoneWindEntityPSOHandle);
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

				ModelAssetHandle modelAssetHandle[5];

				auto windCBHandle = grassService.GetBufferHandle();
				auto footCBHandle = playerService.GetFootBufferHandle();

				auto materialMgr = graphics.GetRenderService()->GetResourceManager<DX11::MaterialManager>();
				// モデルアセットの読み込み
				DX11::ModelAssetCreateDesc modelDesc;
				modelDesc.path = "assets/model/StylizedNatureMegaKit/Rock_Medium_1.gltf";
				modelDesc.pso = cullDefaultPSOHandle;
				modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを設定
				modelDesc.instancesPeak = 1000;
				modelDesc.viewMax = 100.0f;

				modelAssetMgr->Add(modelDesc, modelAssetHandle[0]);

				modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
				modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

				modelDesc.path = "assets/model/Stylized/YellowFlower.gltf";
				modelDesc.buildOccluders = false;
				modelDesc.viewMax = 50.0f;
				modelDesc.pCustomNomWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelAssetMgr->Add(modelDesc, modelAssetHandle[1]);

				modelDesc.path = "assets/model/Stylized/Tree01.gltf";
				modelDesc.viewMax = 100.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.pCustomNomWFunc = WindMovementService::ComputeTreeWeight;
				modelAssetMgr->Add(modelDesc, modelAssetHandle[2]);

				modelDesc.instancesPeak = 100;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.pCustomNomWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.path = "assets/model/Stylized/WhiteCosmos.gltf";
				modelAssetMgr->Add(modelDesc, modelAssetHandle[3]);

				modelDesc.instancesPeak = 100;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.path = "assets/model/Stylized/YellowCosmos.gltf";
				modelAssetMgr->Add(modelDesc, modelAssetHandle[4]);

				ModelAssetHandle playerModelHandle;
				modelDesc.pso = cullDefaultPSOHandle;
				modelDesc.path = "assets/model/BlackGhost.glb";
				modelDesc.pCustomNomWFunc = nullptr;
				modelAssetMgr->Add(modelDesc, playerModelHandle);

				ModelAssetHandle grassModelHandle;

				modelDesc.instancesPeak = 10000;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = windGrassPSOHandle;
				modelDesc.pCustomNomWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.path = "assets/model/Stylized/StylizedGrass.gltf";
				modelAssetMgr->Add(modelDesc, grassModelHandle);
				modelDesc.pCustomNomWFunc = nullptr;

				const auto& serviceLocator = pWorld->GetServiceLocator();
				auto ps = serviceLocator.Get<Physics::PhysicsService>();

				std::function<Physics::ShapeHandle(Math::Vec3f)> makeShapeHandleFunc[5] =
				{
					[&](Math::Vec3f scale)
					{
						return ps->MakeConvexCompound("generated/convex/StylizedNatureMegaKit/Rock_Medium_1.chullbin", true, scale);
					},
					nullptr,
					[&](Math::Vec3f scale)
					{
						Physics::ShapeCreateDesc shapeDesc;
						shapeDesc.shape = Physics::CapsuleDesc{ 8.0f,0.5f };
						shapeDesc.localOffset.y = 8.0f;
						return ps->MakeShape(shapeDesc);
					},
					nullptr,
					nullptr
				};

				// 草のマテリアルに草揺れ用CBVをセット
				{
					//HeightMapを16bitテクスチャとして作成
					size_t heightMapSize = heightMap.size();
					std::vector<uint16_t> height16(heightMapSize);
					// 0.0～1.0 の高さを 16bit に変換
					for (int i = 0; i < heightMapSize; ++i)
					{
						float h01 = heightMap[i]; // 0.0～1.0 の高さ
						h01 = std::clamp(h01, 0.0f, 1.0f);
						height16[i] = static_cast<uint16_t>(h01 * 65535.0f + 0.5f); // 丸め
					}

					DX11::TextureCreateDesc texDesc;
					DX11::TextureRecipe recipeDesc;
					recipeDesc.width = p.cellsX + 1;
					recipeDesc.height = p.cellsZ + 1;
					recipeDesc.format = DXGI_FORMAT_R16_UNORM;
					recipeDesc.usage = D3D11_USAGE_IMMUTABLE;
					recipeDesc.initialData = height16.data();
					recipeDesc.initialRowPitch = recipeDesc.width * (16 / 8);

					texDesc.recipe = &recipeDesc;
					Graphics::TextureHandle heightTexHandle;
					auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
					textureMgr->Add(texDesc, heightTexHandle);

					auto data = modelAssetMgr->GetWrite(grassModelHandle);
					auto& submesh = data.ref().subMeshes;
					auto cbData = bufferMgr->Get(windCBHandle);
					auto footCBData = bufferMgr->Get(footCBHandle);
					auto heightTexData = textureMgr->Get(heightTexHandle);
					for (auto& mesh : submesh)
					{
						auto matData = materialMgr->GetWrite(mesh.material);
						//参照カウントを増やしておく
						bufferMgr->AddRef(windCBHandle);
						//直接定数バッファをセット
						matData.ref().vsCBV.PushOrOverwrite({ 10, cp.cbGrid.Get() });
						matData.ref().vsCBV.PushOrOverwrite({ 11, cbData.ref().buffer.Get() });
						matData.ref().vsCBV.PushOrOverwrite({ 12, footCBData.ref().buffer.Get() });
						matData.ref().usedCBBuffers.push_back(windCBHandle);
						//高さテクスチャもセット
						textureMgr->AddRef(heightTexHandle);
						matData.ref().vsSRV.PushOrOverwrite({ 10, heightTexData.ref().srv.Get() });
						matData.ref().usedTextures.push_back(heightTexHandle);

						//頂点シェーダーにもバインドする設定にする
						matData.ref().isBindVSSampler = true;

						for (auto& tpx : mesh.lodThresholds.Tpx) // LOD調整
						{
							tpx *= 4.0f;
						}
					}
				}

				std::random_device rd;
				std::mt19937_64 rng(rd());

				// 例: A=50%, B=30%, C=20% のつもりで重みを設定（整数でも実数でもOK）
				std::array<int, 5> weights{ 2, 8, 5, 5, 5 };
				std::discrete_distribution<int> dist(weights.begin(), weights.end());

				float modelScaleBase[5] = { 2.5f,1.5f,2.5f, 1.5f,1.5f };
				int modelScaleRange[5] = { 150,25,25, 25,25 };
				int modelRotRange[5] = { 360,360,360, 360,360 };
				bool enableOutline[5] = { true,false,true,false,false };

				std::vector<Math::Vec2f> grassAnchor;
				{
					auto data = modelAssetMgr->Get(grassModelHandle);
					auto aabb = data.ref().subMeshes[0].aabb;
					grassAnchor.reserve(4);
					float bias = 0.8f;
					grassAnchor.push_back({ aabb.lb.x * bias, aabb.lb.z * bias });
					grassAnchor.push_back({ aabb.lb.x * bias, aabb.ub.z * bias });
					grassAnchor.push_back({ aabb.ub.x * bias, aabb.lb.z * bias });
					grassAnchor.push_back({ aabb.ub.x * bias, aabb.ub.z * bias });
				}

				//草Entity生成
				Math::Vec2f terrainScale = {
					p.cellsX * p.cellSize,
					p.cellsZ * p.cellSize
				};

				auto levelSession = pLevel->GetSession();

				for (int j = 0; j < 200; ++j) {
					for (int k = 0; k < 200; ++k) {
						for (int n = 0; n < 1; ++n) {
							float scaleXZ = 15.0f;
							float scaleY = 15.0f;
							Math::Vec2f offsetXZ = { 12.0f,12.0f };
							Math::Vec3f location = { float(j) * scaleXZ / 2.0f + offsetXZ.x , 0, float(k) * scaleXZ / 2.0f + offsetXZ.y };
							auto pose = terrain.SolvePlacementByAnchors(location, 0.0f, scaleXZ, grassAnchor);

							float height = 0.0f;
							terrain.SampleHeightNormalBilinear(location.x, location.z, height);
							location.y = height;

							int col = (int)(std::clamp((location.x / terrainScale.x), 0.0f, 1.0f) * cpuSplatImage.width);
							int row = (int)(std::clamp((location.z / terrainScale.y), 0.0f, 1.0f) * cpuSplatImage.height);

							int byteIndex = col * 4 + row * cpuSplatImage.stride;
							if (byteIndex < 0 || byteIndex >= (int)cpuSplatImage.bytes.size()) {
								continue;
							}

							auto splatR = cpuSplatImage.bytes[byteIndex];
							if (splatR < 20) {
								continue; // 草が薄い場所はスキップ
							}

							//　薄いほど高さを下げる
							location.y -= (1.0f - splatR / 255.0f) * 2.0f - 1.0f;

							auto rot = Math::QuatFromBasis(pose.right, pose.up, pose.forward);
							auto modelComp = CModel{ grassModelHandle };
							rot.KeepTwist(pose.up);
							auto id = levelSession.AddEntity(
								CTransform{ location, rot, Math::Vec3f(scaleXZ,scaleY,scaleXZ) },
								std::move(modelComp)
							);
						}
					}
				}

				// Entity生成
				std::uniform_int_distribution<uint32_t> distX(1, uint32_t(p.cellsX* p.cellSize));
				std::uniform_int_distribution<uint32_t> distZ(1, uint32_t(p.cellsZ* p.cellSize));

				for (int j = 0; j < 200; ++j) {
					for (int k = 0; k < 200; ++k) {
						for (int n = 0; n < 1; ++n) {
							Math::Vec3f location = { (float)distX(rng), 0.0f, (float)distZ(rng)};
							//Math::Vec3f location = { 30.0f * j,0.0f, 10.0f * k };
							auto gridX = (uint32_t)std::floor(location.x / p.cellSize);
							auto gridZ = (uint32_t)std::floor(location.z / p.cellSize);
							if (gridX >= 0 && gridX < p.cellsX - 1 && gridZ >= 0 && gridZ < p.cellsZ - 1)
							{
								float y0 = heightMap[Graphics::TerrainClustered::VIdx(gridX, gridZ, p.cellsX + 1)];
								location.y = y0 * p.heightScale;
								location += p.offset;
							}

							int modelIdx = dist(rng);
							float scale = modelScaleBase[modelIdx] + float(rand() % modelScaleRange[modelIdx] - modelScaleRange[modelIdx] / 2) / 100.0f;
							//float scale = 1.0f;
							auto rot = Math::Quatf::FromAxisAngle({ 0,1,0 }, Math::Deg2Rad(float(rand() % modelRotRange[modelIdx])));
							auto modelComp = CModel{ modelAssetHandle[modelIdx] };
							modelComp.castShadow = true;
							modelComp.outline = enableOutline[modelIdx];

							if (makeShapeHandleFunc[modelIdx] != nullptr)
							{
								auto chunk = pLevel->GetChunk(location);
								auto key = chunk.value()->GetNodeKey();
								SpatialMotionTag tag{};
								tag.handle = { key, chunk.value() };

								Physics::BodyComponent staticBody{};
								staticBody.type = Physics::BodyType::Static; // staticにする
								staticBody.layer = Physics::Layers::NON_MOVING_RAY_IGNORE;
								auto shapeHandle = makeShapeHandleFunc[modelIdx](Math::Vec3f(scale, scale, scale));
#ifdef _DEBUG
								auto shapeDims = ps->GetShapeDims(shapeHandle);
#endif
								auto id = levelSession.AddEntity(
									CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
									modelComp,
									staticBody,
									//Physics::PhysicsInterpolation(
									//	location, // 初期位置
									//	rot // 初期回転
									//),
#ifdef _DEBUG
									shapeDims.value(),
#endif
									tag
								);
								if (id) {
									ps->EnqueueCreateIntent(id.value(), shapeHandle, key);
								}
							}
							else
							{
								levelSession.AddEntity(
									CTransform{ location, rot, Math::Vec3f(scale,scale,scale) },
									modelComp
								);
							}
						}
					}
				}


				//プレイヤー生成
				{
					Math::Vec3f playerStartPos = { 10.0f, 10.0f, 10.0f };

					Physics::ShapeCreateDesc shapeDesc;
					shapeDesc.shape = Physics::CapsuleDesc{ 2.0f, 1.0f };
					shapeDesc.localOffset.y += 2.0f;
					auto playerShape = ps->MakeShape(shapeDesc);
#ifdef _DEBUG
					auto playerDims = ps->GetShapeDims(playerShape);
#endif
					/*auto chunk = pLevel->GetChunk(playerStartPos, EOutOfBoundsPolicy::ClampToEdge);
					auto key = chunk.value()->GetNodeKey();
					SpatialMotionTag tag{};
					tag.handle = { key, chunk.value() };*/

					//Physics::BodyComponent playerBody{};
					//playerBody.isStatic = Physics::BodyType::Dynamic; // 動的にする

					CModel modelComp{ playerModelHandle };
					modelComp.castShadow = true;
					auto id = levelSession.AddGlobalEntity(
						CTransform{ playerStartPos ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } },
						std::move(modelComp),
						PlayerComponent{}
#ifdef _DEBUG
						, playerDims.value()
#endif
					);
					if (id) {
						Physics::CreateCharacterCmd c(id.value());
						c.shape = playerShape;
						c.worldTM.pos = playerStartPos;
						c.objectLayer = Physics::Layers::MOVING;

						ps->CreateCharacter(c);
						//ps->EnqueueCreateIntent(id.value(), playerShape, key);
					}
				}

				//地形コリジョン生成
				{
					Physics::ShapeCreateDesc terrainShapeDesc;
					terrainShapeDesc.shape = Physics::HeightFieldDesc{
						.sizeX = (int)p.cellsX + 1,
						.sizeY = (int)p.cellsZ + 1,
						.samples = heightMap,
						.scaleY = p.heightScale,
						.cellSizeX = p.cellSize,
						.cellSizeY = p.cellSize
					};
					auto terrainShape = ps->MakeShape(terrainShapeDesc);
					Physics::BodyComponent terrainBody{};
					terrainBody.type = Physics::BodyType::Static; // staticにする
					terrainBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;
					auto id = levelSession.AddEntity(
						CTransform{ 0.0f, -40.0f, 0.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
						terrainBody
						//,
						//Physics::PhysicsInterpolation(
						//	Math::Vec3f{ 0.0f,-40.0f, 0.0f }, // 初期位置
						//	Math::Quatf{ 0.0f,0.0f,0.0f,1.0f } // 初期回転
						//)
					);
					if (id) {
						auto chunk = pLevel->GetChunk({ 0.0f, -40.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
						ps->EnqueueCreateIntent(id.value(), terrainShape, chunk.value()->GetNodeKey());
					}
				}

				// System登録
				auto& scheduler = pLevel->GetScheduler();

				scheduler.AddSystem<ModelRenderSystem>(serviceLocator);

				//scheduler.AddSystem<SimpleModelRenderSystem>(serviceLocator);
				scheduler.AddSystem<CameraSystem>(serviceLocator);
				//scheduler.AddSystem<PhysicsSystem>(serviceLocator);
				scheduler.AddSystem<BuildBodiesFromIntentsSystem>(serviceLocator);
				scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(serviceLocator);
				scheduler.AddSystem<DebugRenderSystem>(serviceLocator);
				//scheduler.AddSystem<PlayerSystem>(serviceLocator);
				scheduler.AddSystem<EnviromentSystem>(serviceLocator);
				//scheduler.AddSystem<CleanModelSystem>(serviceLocator);

			},
			//アンロード時
			[&](std::add_pointer_t<decltype(world)> pWorld, OpenFieldLevel* pLevel)
			{
				isExecuteCustomFunc.store(false, std::memory_order_relaxed);
			});
	}

	//初めのレベルをロード
	world.GetSession().LoadLevel("Title");
	world.GetSession().LoadLevel("OpenField");

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	//シーンロードのデバッグコールバック登録
	{
		static std::string loadLevelName;

		BIND_DEBUG_TEXT("Level", "Name", &loadLevelName);

		REGISTER_DEBUG_BUTTON("Level", "load", [](bool) {
			auto worldSession = gameEngine.GetWorld().GetSession();
			worldSession.LoadLevel(loadLevelName);
			});

		REGISTER_DEBUG_BUTTON("Level", "clean", [](bool) {
			auto worldSession = gameEngine.GetWorld().GetSession();
			worldSession.CleanLevel(loadLevelName);
			});
	}
	//スレッドプールクラス
	static SimpleThreadPool threadPool;

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop(&threadPool);
		});

	return WindowHandler::Destroy();
}