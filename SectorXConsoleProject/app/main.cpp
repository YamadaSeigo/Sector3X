
/*****************************************************************//**
 * @file   main.cpp
 * @brief SectorX コンソールプロジェクトのエントリーポイント
 * @author seigo_t03b63m
 * @date   December 2025
 *********************************************************************/

//========================================================================
// 一人のプロジェクトなので、とりあえず初期化関連の処理をmainに書いています
// 将来的には適切に分割する
//========================================================================

//SectorFW
#include <SectorFW/Debug/ImGuiBackendDX11Win32.h>
#include <SectorFW/Core/ChunkCrossingMove.hpp>
#include <SectorFW/DX11WinTerrainHelper.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>
#include <SectorFW/Graphics/TerrainOccluderExtraction.h>
#include <SectorFW/Graphics/ImageLoader.h>

//System
#include "system/CameraSystem.h"
#include "system/ModelRenderSystem.h"
#include "system/PhysicsSystem.h"
#include "system/BuildBodiesFromIntentsSystem.hpp"
#include "system/BodyIDWriteBackFromEventSystem.hpp"
#include "system/DebugRenderSystem.h"
#include "system/GlobalDebugRenderSystem.h"
#include "system/CleanModelSystem.h"
#include "system/SimpleModelRenderSystem.h"
#include "system/SpriteRenderSystem.h"
#include "system/PlayerSystem.h"
#include "system/EnvironmentSystem.h"
#include "system/DeferredRenderingSystem.h"
#include "system/LightShadowSystem.h"
#include "system/PointLightSystem.h"
#include "system/SpriteAnimationSystem.h"
#include "system/FireflySystem.h"
#include "WindMovementService.h"
#include "EnvironmentService.h"

#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>

#include <string>

//デバッグ用
#include <immintrin.h> // AVX2, AVX512など

#define WINDOW_NAME "SectorX Console Project"

//4の倍数
constexpr uint32_t WINDOW_WIDTH = uint32_t(1920 / 1.5f);	// ウィンドウの幅
constexpr uint32_t WINDOW_HEIGHT = uint32_t(1080 / 1.5f);	// ウィンドウの高さ

constexpr uint32_t SHADOW_MAP_SIZE = 1024 / 2;	// シャドウマップの幅

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
	{ Mat_Rock,  { "assets/texture/terrain/RockHigh.jpg",  true } },
	{ Mat_Dirt,  { "assets/texture/terrain/DirtHigh.png",  true } },
	{ Mat_Snow,  { "assets/texture/terrain/snow.png",  true } },
};

// 素材以外（スプラット重み等）は “テクスチャID” テーブルで受ける
static std::unordered_map<uint32_t, std::pair<std::string, bool>> gTextures = {
	// 重みテクスチャは “非 sRGB” 推奨（正規化済みのスカラー重みだから）
	{ Tex_Splat_Control_0, { "assets/texture/terrain/splat_thin.png", false } },
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

//カスタム関数を実行するかのフラグ(MainのLevelロード完了時にtrueにしている)
static std::atomic<bool> isExecuteCustomFunc = false;

struct RtPack
{
	std::vector<ComPtr<ID3D11RenderTargetView>> rtv;
	std::vector<ComPtr<ID3D11ShaderResourceView>> srv;
};

bool CreateMRT(ID3D11Device* dev, Graphics::DX11::TextureManager* texMgr, const DeferredRenderingService& deferredService, RtPack& out)
{
	using namespace Graphics;

	out.rtv.resize(DeferredTextureCount);
	out.srv.resize(DeferredTextureCount);

	for (int i = 0; i < DeferredTextureCount; ++i)
	{
		const auto& texData = texMgr->Get(deferredService.GetGBufferHandles()[i]);

		out.srv[i] = texData.ref().srv;

		HRESULT hr;

		hr = dev->CreateRenderTargetView(texData.ref().resource.Get(), nullptr, out.rtv[i].GetAddressOf());
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
	ComPtr<ID3D11ShaderResourceView>& mainDepthStencilSRV,
	Graphics::PassCustomFuncType drawTerrainColor,
	Graphics::PassCustomFuncType drawFirefly,
	Graphics::DX11::LightShadowResourceService* resourceService,
	const DeferredRenderingService& deferredService)
{
	using namespace SFW::Graphics;

	auto bufferMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::BufferManager>();
	auto cameraHandle3D = bufferMgr->FindByName(DX11::PerCamera3DService::BUFFER_NAME);
	auto cameraHandle2D = bufferMgr->FindByName(DX11::Camera2DService::BUFFER_NAME);

	auto shaderMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::ShaderManager>();
	auto psoMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::PSOManager>();
	auto textureMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::TextureManager>();

	static RtPack ttMRT;
	bool ok = CreateMRT(graphics->GetDevice(), textureMgr, deferredService, ttMRT);
	assert(ok);

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

	const auto& cascadeDSVs = resourceService->GetCascadeDSV();

	auto cascadeCount = cascadeDSVs.size();

	DX11::BufferCreateDesc cbDesc;
	cbDesc.size = sizeof(CascadeIndex);

	Viewport vp;
	vp.width = (float)SHADOW_MAP_SIZE;
	vp.height = (float)SHADOW_MAP_SIZE;

	passDesc.viewport = vp;

	RasterizerStateID shadowRasterizerStates[Graphics::kMaxShadowCascades] = {
		RasterizerStateID::ShadowBiasLow,
		RasterizerStateID::ShadowBiasMedium,
		RasterizerStateID::ShadowBiasHigh
	};

	passDesc.rebindPSO = true;

	for (UINT i = 0; i < cascadeCount; ++i)
	{
		cbDesc.name = "CascadeIndexCB_" + std::to_string(i);
		CascadeIndex data;
		data.index = i;

		cbDesc.initialData = &data;
		BufferHandle cascadeIndexHandle;
		bufferMgr->Add(cbDesc, cascadeIndexHandle);
		passDesc.cbvs = { BindSlotBuffer{13, cascadeIndexHandle} };

		passDesc.rasterizerState = shadowRasterizerStates[i];
		passDesc.dsv = cascadeDSVs[i];

		renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_CASCADE0 << i);
	}

	passDesc.rebindPSO = false;
	shaderDesc.vsPath = L"assets/shader/VS_ZPrepass.cso";

	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoMgr->Add(psoDesc, psoHandle);

	vp.width = (float)WINDOW_WIDTH;
	vp.height = (float)WINDOW_HEIGHT;
	passDesc.viewport = vp;

	passDesc.dsv = mainDepthStencilView;
	passDesc.cbvs = { BindSlotBuffer{cameraHandle3D} };
	passDesc.rasterizerState = std::nullopt;
	passDesc.psoOverride = psoHandle;
	passDesc.customExecute = {};

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

	//本当はやるべきではないがここから
	//staticであることを前提にしてラムダ式でキャプチャなしでアクセスするために所有権保持

	static auto gGraphics = graphics;
	static auto renderBackend = graphics->GetBackend();
	static auto lightShadowService = resourceService;

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

	DX11::ShaderCreateDesc defferedShaderDesc;
	defferedShaderDesc.vsPath = L"assets/shader/VS_Fullscreen.cso";
	defferedShaderDesc.psPath = L"assets/shader/PS_PBR_Unlit_Shadow.cso";
	ShaderHandle defferedShaderHandle = {};
	auto shaderData = shaderMgr->CreateResource(defferedShaderDesc, defferedShaderHandle);
	static ComPtr<ID3D11VertexShader> defferedVS = shaderData.vs;
	static ComPtr<ID3D11PixelShader> defferedPS = shaderData.ps;

	static std::vector<ID3D11ShaderResourceView*> derreredSRVs;
	for (auto& srv : ttMRT.srv)
	{
		derreredSRVs.push_back(srv.Get());
	}

	// 深度ステンシルSRVも追加
	derreredSRVs.push_back(mainDepthStencilSRV.Get());

	static std::vector<ID3D11ShaderResourceView*> nullSRVs;
	for (int i = 0; i < derreredSRVs.size(); ++i)
	{
		nullSRVs.push_back(nullptr);
	}

	auto deferredCameraHandle = bufferMgr->FindByName(DeferredRenderingService::BUFFER_NAME);

	static ComPtr<ID3D11Buffer> invCameraBuffer;
	{
		auto bufData = bufferMgr->Get(deferredCameraHandle);
		invCameraBuffer = bufData.ref().buffer;
	}

	auto samplerManager = renderGraph->GetRenderService()->GetResourceManager<DX11::SamplerManager>();

	static ComPtr<ID3D11SamplerState> linearSampler;
	{
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		SamplerHandle samp = samplerManager->AddWithDesc(sampDesc);
		auto sampData = samplerManager->Get(samp);
		linearSampler = sampData.ref().state;
	}

	static ComPtr<ID3D11SamplerState> pointSampler;
	{
		D3D11_SAMPLER_DESC sampDesc;
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 全部 POINT
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;    // 0～1 の外はクランプ
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.MipLODBias = 0.0f;
		sampDesc.MaxAnisotropy = 1;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;        // 普通のサンプラなので ALWAYS
		sampDesc.BorderColor[0] = 0.0f;
		sampDesc.BorderColor[1] = 0.0f;
		sampDesc.BorderColor[2] = 0.0f;
		sampDesc.BorderColor[3] = 0.0f;
		sampDesc.MinLOD = 0.0f;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		SamplerHandle samp = samplerManager->AddWithDesc(sampDesc);
		auto sampData = samplerManager->Get(samp);
		pointSampler = sampData.ref().state;
	}

	/*static ComPtr<ID3D11SamplerState> shadowSampler;
	shadowSampler = resourceService.GetShadowSampler();

	static ComPtr<ID3D11Buffer> cbLightBuffer;
	cbLightBuffer = resourceService.GetLightDataCB();
	static ComPtr<ID3D11ShaderResourceView> srvPointLight;
	srvPointLight = resourceService.GetPointLightSRV();*/

	static ComPtr<ID3D11Buffer> fogBuffer;

	static ComPtr<ID3D11Buffer> godRayBuffer;

	auto fogBufHandle = bufferMgr->FindByName(EnvironmentService::FOG_BUFFER_NAME);
	auto fogBufData = bufferMgr->Get(fogBufHandle);

	auto godRayBufHandle = bufferMgr->FindByName(EnvironmentService::GODRAY_BUFFER_NAME);
	auto godRayBufData = bufferMgr->Get(godRayBufHandle);

	fogBuffer = fogBufData.ref().buffer;
	godRayBuffer = godRayBufData.ref().buffer;

	auto drawSky = [](uint64_t frame) {
		if(!isExecuteCustomFunc.load(std::memory_order_relaxed)) return;

		skyboxData.time += (float)(1.0f / FPS_LIMIT);
		renderBackend->UpdateBufferDataImpl(SkyCBBuffer.Get(), &skyboxData, sizeof(skyboxData));
		gGraphics->SetDepthStencilState(DepthStencilStateID::DepthReadOnly);
		renderBackend->BindPSCBVs({ SkyCBBuffer.Get() }, 9);
		renderBackend->DrawInstanced(skyboxMeshHandle.index, skyboxMaterialHandle.index, skyboxPsoHandle.index, 1, true, false);
		};

	auto createScreenTex = [&](ComPtr<ID3D11ShaderResourceView>& outSRV, ComPtr<ID3D11RenderTargetView>& outRTV, DXGI_FORMAT format, uint32_t w, uint32_t h, TextureHandle* outH = nullptr) {

		DX11::TextureRecipe recipe;
		recipe.width = w;
		recipe.height = h;
		recipe.format = format;
		recipe.mipLevels = 1;
		recipe.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		recipe.usage = D3D11_USAGE_DEFAULT;
		recipe.arraySize = 1;
		DX11::TextureCreateDesc texDesc;
		texDesc.recipe = &recipe;

		DX11::TextureData texData;

		if(outH) {
			TextureHandle texHandle = {};
			textureMgr->Add(texDesc, texHandle);
			auto src = textureMgr->Get(texHandle);
			texData = src.ref();
			*outH = texHandle;
		}
		else {
			TextureHandle texHandle = {};
			texData = textureMgr->CreateResource(texDesc, texHandle);
		}

		outSRV = texData.srv;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		HRESULT hr;
		hr = graphics->GetDevice()->CreateRenderTargetView(texData.resource.Get(), &rtvDesc, outRTV.GetAddressOf());
		assert(SUCCEEDED(hr));
		};

	auto compileShaderPS = [&](const wchar_t* psPath, ComPtr<ID3D11PixelShader>& outPS)
		{
			ComPtr<ID3DBlob> psBlob;
			HRESULT hr = D3DReadFileToBlob(psPath, psBlob.GetAddressOf());
#ifdef _DEBUG
			std::string msgPath = SFW::WCharToUtf8_portable(psPath);
			DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
			hr = graphics->GetDevice()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, outPS.GetAddressOf());
			assert(SUCCEEDED(hr) && "Failed to create pixel shader.");
		};

	//　解像度を落とすことでにじみ表現と負荷軽減
	constexpr uint32_t BLOOM_TEX_WIDTH = WINDOW_WIDTH / 2;
	constexpr uint32_t BLOOM_TEX_HEIGHT = WINDOW_HEIGHT / 2;

	static ComPtr<ID3D11ShaderResourceView> sceneColorSRV;
	static ComPtr<ID3D11RenderTargetView> sceneColorRTV;
	// 1.0以上でも潰れないフォーマット
	createScreenTex(sceneColorSRV, sceneColorRTV, DXGI_FORMAT_R11G11B10_FLOAT, WINDOW_WIDTH, WINDOW_HEIGHT);

	static ComPtr<ID3D11ShaderResourceView> brightSRV;
	static ComPtr<ID3D11RenderTargetView> brightRTV;
	createScreenTex(brightSRV, brightRTV, DXGI_FORMAT_R8G8B8A8_UNORM, BLOOM_TEX_WIDTH, BLOOM_TEX_HEIGHT);

	static ComPtr<ID3D11ShaderResourceView> bloomSRV;
	static ComPtr<ID3D11RenderTargetView> bloomRTV;
	//デバックのためにハンドルを保持しておく
	createScreenTex(bloomSRV, bloomRTV, DXGI_FORMAT_R8G8B8A8_UNORM, BLOOM_TEX_WIDTH, BLOOM_TEX_HEIGHT, &DebugRenderType::debugBloomTexHandle);

	static ComPtr<ID3D11PixelShader> brightPS;
	static ComPtr<ID3D11PixelShader> bloomHPS;
	static ComPtr<ID3D11PixelShader> compositePS;
	compileShaderPS(L"assets/shader/PS_BrightExtract.cso", brightPS);
	compileShaderPS(L"assets/shader/PS_BlurH.cso", bloomHPS);
	compileShaderPS(L"assets/shader/PS_Composite.cso", compositePS);

	struct BloomCB {
		float gBloomThreshold; // 例: 1.0（HDR前提） / LDRなら0.8等
		float gBloomKnee; // 例: 0.5（soft threshold）
		float gBloomIntensity; // 合成側で使ってもOK
		float gBloomMaxDist;
	};

	static BloomCB cpuBloomData = { 1.0f, 0.5f, 1.0f, 200.0f };
	static bool bloomDataChanged = true;
	REGISTER_DEBUG_SLIDER_FLOAT("Bloom", "threshold", cpuBloomData.gBloomThreshold, 0.0f, 1.0f, 0.001f, [](float value) {
		bloomDataChanged = true; cpuBloomData.gBloomThreshold = value;
		});
	REGISTER_DEBUG_SLIDER_FLOAT("Bloom", "knee", cpuBloomData.gBloomKnee, 0.0f, 1.0f, 0.001f, [](float value) {
		bloomDataChanged = true; cpuBloomData.gBloomKnee = value;
		});
	REGISTER_DEBUG_SLIDER_FLOAT("Bloom", "intensity", cpuBloomData.gBloomIntensity, 0.0f, 5.0f, 0.01f, [](float value) {
		bloomDataChanged = true; cpuBloomData.gBloomIntensity = value;
		});
	REGISTER_DEBUG_SLIDER_FLOAT("Bloom", "distance", cpuBloomData.gBloomMaxDist, 0.0f, 400.0f, 0.1f, [](float value) {
		bloomDataChanged = true; cpuBloomData.gBloomMaxDist = value;
		});

	DX11::BufferCreateDesc cbBloomDesc;
	cbBloomDesc.name = "BloomCB";
	cbBloomDesc.size = sizeof(BloomCB);
	cbBloomDesc.initialData = &cpuBloomData;
	BufferHandle bloomCBHandle = {};
	auto bloomBufData = bufferMgr->CreateResource(cbBloomDesc, bloomCBHandle);

	static ComPtr<ID3D11Buffer> bloomCBBuffer = bloomBufData.buffer;

	struct BlurCB {
		Math::Vec2f gTexelSize; // 1/width, 1/height
		Math::Vec2f _pad;
	};

	static BlurCB cpuBlurData = { Math::Vec2f(1.0f / BLOOM_TEX_WIDTH, 1.0f / BLOOM_TEX_HEIGHT), Math::Vec2f(0.0f, 0.0f) };
	DX11::BufferCreateDesc cbBlurDesc;
	cbBlurDesc.name = "BlurCB";
	cbBlurDesc.size = sizeof(BlurCB);
	cbBlurDesc.initialData = &cpuBlurData;
	BufferHandle blurCBHandle = {};
	auto blurBufData = bufferMgr->CreateResource(cbBlurDesc, blurCBHandle);
	static ComPtr<ID3D11Buffer> blurCBBuffer = blurBufData.buffer;

	auto drawFullScreen = [](uint64_t frame) {

		// 全画面描画でライティング計算
		auto ctx = gGraphics->GetDeviceContext();

		gGraphics->SetBlendState(BlendStateID::Opaque);
		gGraphics->SetRasterizerState(RasterizerStateID::SolidCullBack);
		gGraphics->SetDepthStencilState(DepthStencilStateID::DepthReadOnly);

		ctx->IASetInputLayout(nullptr);
		ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//(1) ディファ―ドのフルスクリーン合成
		//=========================================================================

		// Depthはsrvとして使うので外す
		ctx->OMSetRenderTargets(1, sceneColorRTV.GetAddressOf(), nullptr);

		//CBの5にシャドウマップのパラメーター, Samplerを1にバインド
		lightShadowService->BindShadowResources(ctx, 5);
		//　シャドウマップバインド
		lightShadowService->BindShadowPSShadowMap(ctx, 7);

		ctx->PSSetShaderResources(11, (UINT)derreredSRVs.size(), derreredSRVs.data());
		ctx->PSSetShaderResources(15, 1, lightShadowService->GetPointLightSRV().GetAddressOf());
		renderBackend->BindPSCBVs({ SkyCBBuffer.Get(), invCameraBuffer.Get(), lightShadowService->GetLightDataCB().Get(), fogBuffer.Get(), godRayBuffer.Get()}, 9);

		ctx->PSSetSamplers(0, 1, linearSampler.GetAddressOf());
		ctx->PSSetSamplers(1, 1, lightShadowService->GetShadowSampler().GetAddressOf());
		ctx->PSSetSamplers(2, 1, pointSampler.GetAddressOf());

		ctx->VSSetShader(defferedVS.Get(), nullptr, 0);
		ctx->PSSetShader(defferedPS.Get(), nullptr, 0);

		ctx->Draw(3, 0);

		// SRVを解除
		ctx->PSSetShaderResources(11, (UINT)nullSRVs.size(), nullSRVs.data());

		//(2) ブライト抽出
		//=========================================================================

		if (bloomDataChanged) {
			bloomDataChanged = false;

			D3D11_MAPPED_SUBRESOURCE mapped;
			HRESULT hr = ctx->Map(bloomCBBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			if (SUCCEEDED(hr))
			{
				memcpy(mapped.pData, &cpuBloomData, sizeof(BloomCB));
				ctx->Unmap(bloomCBBuffer.Get(), 0);
			}
		}

		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = static_cast<FLOAT>(BLOOM_TEX_WIDTH);
		vp.Height = static_cast<FLOAT>(BLOOM_TEX_HEIGHT);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		ctx->OMSetRenderTargets(1, brightRTV.GetAddressOf(), nullptr);

		ctx->PSSetConstantBuffers(0, 1, bloomCBBuffer.GetAddressOf());

		ctx->PSSetConstantBuffers(1, 1, invCameraBuffer.GetAddressOf());

		ctx->PSSetShaderResources(0, 1, sceneColorSRV.GetAddressOf());

		//DepthをSRVとしてバインド
		ctx->PSSetShaderResources(1, 1, &derreredSRVs.back());

		ctx->PSSetSamplers(0, 1, linearSampler.GetAddressOf());

		ctx->PSSetShader(brightPS.Get(), nullptr, 0);

		ctx->Draw(3, 0);

		// SRVを解除
		ctx->PSSetShaderResources(0, 1, nullSRVs.data());

		//(3) ボケ（横）
		//=========================================================================

		ctx->OMSetRenderTargets(1, bloomRTV.GetAddressOf(), nullptr);

		ctx->PSSetConstantBuffers(0, 1, blurCBBuffer.GetAddressOf());

		ctx->PSSetShaderResources(0, 1, brightSRV.GetAddressOf());

		ctx->PSSetSamplers(0, 1, linearSampler.GetAddressOf());

		ctx->PSSetShader(bloomHPS.Get(), nullptr, 0);

		ctx->Draw(3, 0);

		ctx->PSSetShaderResources(0, 1, nullSRVs.data());

		//(4) 合成
		//=========================================================================

		gGraphics->SetMainRenderTargetNoDepth();

		ctx->RSSetViewports(1, &gGraphics->GetMainViewport());

		ctx->PSSetConstantBuffers(0, 1, bloomCBBuffer.GetAddressOf());

		ctx->PSSetShaderResources(0, 1, sceneColorSRV.GetAddressOf());

		ctx->PSSetShaderResources(1, 1, bloomSRV.GetAddressOf());

		ctx->PSSetSamplers(0, 1, linearSampler.GetAddressOf());

		ctx->PSSetShader(compositePS.Get(), nullptr, 0);

		ctx->Draw(3, 0);

		// SRVを解除
		ctx->PSSetShaderResources(0, 2, nullSRVs.data());

		//=========================================================================

		gGraphics->SetMainRenderTargetAndDepth();

		};


	passDesc.rtvs = ttMRT.rtv;
	passDesc.dsv = mainDepthStencilView;
	passDesc.cbvs = { BindSlotBuffer{cameraHandle3D} };
	passDesc.psoOverride = std::nullopt;
	passDesc.viewport = vp;
	passDesc.depthStencilState = DepthStencilStateID::Default_Stencil;
	passDesc.customExecute = { drawTerrainColor };
	passDesc.stencilRef = 1;

	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_OUTLINE);

	passDesc.customExecute = { drawSky, drawFirefly, drawFullScreen};
	passDesc.stencilRef = 2;
	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_OPAQUE);

	shaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
	shaderDesc.psPath = L"assets/shader/PS_Alpha.cso";
	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoDesc.rasterizerState = RasterizerStateID::SolidCullBack;
	psoMgr->Add(psoDesc, psoHandle);

	passDesc.rtvs = mainRtv;
	passDesc.customExecute = {};
	passDesc.psoOverride = psoHandle;
	passDesc.blendState = BlendStateID::AlphaBlend;
	passDesc.depthStencilState = DepthStencilStateID::DepthReadOnly;
	passDesc.stencilRef = 2;
	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_TRANSPARENT);

	shaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
	shaderDesc.psPath = L"assets/shader/PS_HighLight.cso";
	shaderMgr->Add(shaderDesc, shaderHandle);
	psoDesc.shader = shaderHandle;
	psoDesc.rasterizerState = RasterizerStateID::SolidCullBack;
	psoMgr->Add(psoDesc, psoHandle);

	passDesc.rtvs = mainRtv;
	passDesc.customExecute = {};
	passDesc.psoOverride = psoHandle;
	passDesc.blendState = BlendStateID::Opaque;
	passDesc.depthStencilState = DepthStencilStateID::DepthReadOnly_Greater_Read_Stencil;
	passDesc.stencilRef = 1;
	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_HIGHLIGHT);

	auto& UIGroup = renderGraph->AddPassGroup(PassGroupName[GROUP_UI]);

	passDesc.viewport = std::nullopt;
	passDesc.customExecute = {};

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
		{ 0, 7 },
		{ 1, 0 },
		{ 1, 1 },
		{ 1, 2 }
	};

	renderGraph->SetExecutionOrder(order);
}

// [-1,1] の値を UNORM 0..255 に変換
inline uint8_t NormalToUNorm8(float v)
{
	// [-1,1] -> [0,1]
	float u = v * 0.5f + 0.5f;
	u = std::clamp(u, 0.0f, 1.0f);
	return static_cast<uint8_t>(std::round(u * 255.0f));
}

// 4x4 texel の 1チャンネル (UNORM 0..255) から BC4 ブロック(8バイト)を生成
// src[16] : 4x4 の各画素 (0..255)
inline void EncodeBC4Block(const uint8_t src[16], uint8_t dst[8])
{
	// 1) ブロック内の最小値・最大値
	uint8_t vMin = 255;
	uint8_t vMax = 0;

	for (int i = 0; i < 16; ++i)
	{
		vMin = (std::min)(vMin, src[i]);
		vMax = (std::max)(vMax, src[i]);
	}

	// 全部同じ値なら、エッジケース処理
	if (vMin == vMax)
	{
		dst[0] = vMax;
		dst[1] = vMin;
		// インデックスは全部 0 にしておく
		for (int i = 0; i < 6; ++i) dst[i + 2] = 0;
		return;
	}

	// 2) エンドポイントを決める
	// BC4_UNORM では 0..255 のエンドポイント
	uint8_t ep0 = vMax;
	uint8_t ep1 = vMin;

	// 「ep0 > ep1」の 8 ステップモードを強制
	if (ep0 == ep1)
	{
		// 万一同じだったら適当にずらす
		if (ep0 < 255) ++ep0;
		else --ep1;
	}

	dst[0] = ep0;
	dst[1] = ep1;

	// 3) 8 エントリーのパレット生成
	float palette[8];
	palette[0] = ep0 / 255.0f;
	palette[1] = ep1 / 255.0f;
	for (int i = 1; i <= 6; ++i)
	{
		// 8 ステップモード: v_i = ((7-i)*ep0 + i*ep1) / 7
		float v = ((7 - i) * ep0 + i * ep1) / 7.0f;
		palette[i + 1] = v / 255.0f;
	}

	// 4) 各 texel の最適な index(0..7) を決める
	uint8_t indices[16];

	for (int i = 0; i < 16; ++i)
	{
		float val = src[i] / 255.0f;
		float bestErr = 1e9f;
		uint8_t bestIdx = 0;
		for (uint8_t j = 0; j < 8; ++j)
		{
			float d = val - palette[j];
			float err = d * d;
			if (err < bestErr)
			{
				bestErr = err;
				bestIdx = j;
			}
		}
		indices[i] = bestIdx;
	}

	// 5) 16 個の 3bit index を 48bit (6 バイト) にパック
	uint64_t bits = 0;
	for (int i = 0; i < 16; ++i)
	{
		bits |= (uint64_t(indices[i] & 7u) << (3 * i));
	}

	for (int i = 0; i < 6; ++i)
	{
		dst[2 + i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu);
	}
}


inline std::vector<uint8_t> EncodeNormalMapBC5(
	const Math::Vec3f* normals,
	int width,
	int height)
{
	assert(normals);
	assert(width > 0 && height > 0);
	assert((width % 4) == 0 && (height % 4) == 0); // 4 の倍数前提

	const int blockCountX = width / 4;
	const int blockCountY = height / 4;
	const int totalBlocks = blockCountX * blockCountY;

	std::vector<uint8_t> out;
	out.resize(size_t(totalBlocks) * 16); // 16 bytes per block (BC5)

	uint8_t blockR[16];
	uint8_t blockG[16];

	size_t dstOffset = 0;

	for (int by = 0; by < blockCountY; ++by)
	{
		for (int bx = 0; bx < blockCountX; ++bx)
		{
			// 4x4 ブロック分の R,G 成分を UNORM 0..255 に変換して blockR/blockG に詰める
			for (int iy = 0; iy < 4; ++iy)
			{
				for (int ix = 0; ix < 4; ++ix)
				{
					int x = bx * 4 + ix;
					int y = by * 4 + iy;
					int srcIdx = y * width + x;
					const Math::Vec3f& n = normals[srcIdx];

					int texelIndex = iy * 4 + ix;
					blockR[texelIndex] = NormalToUNorm8(n.x); // R チャンネル
					blockG[texelIndex] = NormalToUNorm8(n.z); // G チャンネル
				}
			}

			uint8_t* dstBlock = out.data() + dstOffset;

			// 先頭 8バイト: R チャンネルの BC4
			EncodeBC4Block(blockR, dstBlock + 0);
			// 次の 8バイト: G チャンネルの BC4
			EncodeBC4Block(blockG, dstBlock + 8);

			dstOffset += 16;
		}
	}

	return out;
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
	ComponentTypeRegistry::Register<Physics::CPhyBody>();
	ComponentTypeRegistry::Register<Physics::PhysicsInterpolation>();
	ComponentTypeRegistry::Register<Physics::ShapeDims>();
	ComponentTypeRegistry::Register<CSprite>();
	ComponentTypeRegistry::Register<CPointLight>();
	ComponentTypeRegistry::Register<CSpriteAnimation>();
	ComponentTypeRegistry::Register<CFireflyVolume>();
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
	auto textureManager = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>();
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
	cascadeConfig.shadowMapResolution = Math::Vec2f(float(SHADOW_MAP_SIZE), float(SHADOW_MAP_SIZE));
	cascadeConfig.cascadeCount = 3;
	cascadeConfig.shadowDistance = 80.0f;
	cascadeConfig.casterExtrusion = 0.0f;
	lightShadowService.SetCascadeConfig(cascadeConfig);

	WindMovementService grassService(bufferMgr);

	PlayerService playerService(bufferMgr);

	Audio::AudioService audioService;
	ok = audioService.Initialize();
	assert(ok && "Failed Audio Service Initialize");

	DeferredRenderingService deferredRenderingService(bufferMgr, textureManager, WINDOW_WIDTH, WINDOW_HEIGHT);

	static Graphics::DX11::LightShadowResourceService lightShadowResourceService;
	Graphics::DX11::ShadowMapConfig shadowMapConfig;
	shadowMapConfig.width = SHADOW_MAP_SIZE;
	shadowMapConfig.height = SHADOW_MAP_SIZE;
	ok = lightShadowResourceService.Initialize(device, shadowMapConfig);
	assert(ok && "Failed ShadowMapService Initialize");

	static Graphics::PointLightService pointLightService;

	EnvironmentService environmentService(bufferMgr);

	static SpriteAnimationService spriteAnimationService(bufferMgr);

	//地形のコンピュートと同じタイミングで描画する
	static FireflyService fireflyService(device, deviceContext, bufferMgr,
		L"assets/shader/CS_FireflyInitFreeList.cso",
		L"assets/shader/CS_FireflySpawn.cso",
		L"assets/shader/CS_FireflyUpdate.cso",
		L"assets/shader/CS_FireflyArgs.cso",
		L"assets/shader/VS_FireflyBillboard.cso",
		L"assets/shader/PS_Firefly.cso");

	ECS::ServiceLocator serviceLocator(
		renderService, &physicsService, inputService, perCameraService,
		ortCameraService, camera2DService, &lightShadowService, &grassService,
		&playerService, &audioService, &deferredRenderingService, &lightShadowResourceService,
		&pointLightService, &environmentService, &spriteAnimationService, &fireflyService);

	serviceLocator.InitAndRegisterStaticService<SpatialChunkRegistry>();

	//スレッドプールクラス
	static std::unique_ptr<SimpleThreadPool> threadPool = std::make_unique<SimpleThreadPool>();

	enum class TerrainRank : int {
		Low = 1,
		Middle = 2,
		High = 4
	};

	int terrainRank = (int)TerrainRank::High;

	// Rチャンネルだけ使う想定
	//auto DesignerHeightMapImg = Graphics::LoadImageFromFile(
	//	"assets/texture/terrain/dontuse.png", 1, false);

	//// 地形の属性をもった高さマップ
	//Graphics::DesignerHeightMap designerHeightMap;
	//designerHeightMap.width = DesignerHeightMapImg.width;
	//designerHeightMap.height = DesignerHeightMapImg.height;
	//designerHeightMap.data.resize(designerHeightMap.width* designerHeightMap.height);
	//for(auto y = 0; y < DesignerHeightMapImg.height; ++y)
	//{
	//	for (auto x = 0; x < DesignerHeightMapImg.width; ++x)
	//	{
	//		auto index = y * DesignerHeightMapImg.width + x;
	//		uint8_t r = (uint8_t)DesignerHeightMapImg.pixels.get()[index];
	//		designerHeightMap.data[index] = static_cast<float>(r) / 255.0f;
	//	}
	//}


	Graphics::TerrainBuildParams p;
	p.cellsX = 256 * terrainRank - 1;
	p.cellsZ = 256 * terrainRank - 1;
	p.clusterCellsX = 32;
	p.clusterCellsZ = 32;
	p.cellSize = 3.0f;
	p.heightScale = 80.0f;
	p.frequency = 1.0f / 90.0f;
	p.seed = 20251212;
	p.offset.y -= 40.0f;
	//p.designer = &designerHeightMap;

	//端まで見えるように
	//perCameraService->SetFarClip(p.cellsX * p.cellSize);

	std::vector<float> heightMap;
	static SFW::Graphics::TerrainClustered terrain = Graphics::TerrainClustered::Build(p, &heightMap);

	// ---- マッピング設定(オクルージョンカリング用) ----
	static Graphics::HeightTexMapping heightTexMap = Graphics::MakeHeightTexMappingFromTerrainParams(p, heightMap);

	static Graphics::DX11::CommonMaterialResources matRes;
	const uint32_t matIds[4] = { Mat_Grass, Mat_Rock, Mat_Dirt, Mat_Snow }; //素材ID
	Graphics::DX11::BuildCommonMaterialSRVs(device, *textureManager, matIds, &ResolveTexturePath, matRes);

	// 0) “シート画像” の ID（例: Tex_Splat_Sheet0）は ResolveTexturePathFn でパスに解決される想定
	uint32_t sheetTexId = Tex_Splat_Control_0;

	ComPtr<ID3D11Texture2D> sheetTex;
	// 1) シートを分割して各クラスタの TextureHandle を生成
	auto handles = Graphics::DX11::BuildClusterSplatTexturesFromSingleSheet(
		device, deviceContext, *textureManager,
		sheetTex,
		terrain.clustersX, terrain.clustersZ,
		sheetTexId, &ResolveTexturePath,
		/*sheetIsSRGB=*/false // 重みなので通常は false
	);


	// 2) 生成ハンドルにアプリ側の “ID” を割当て -> terrain.splat[cid].splatTextureId に反映
	Graphics::DX11::AssignClusterSplatsFromHandles(terrain, terrain.clustersX, terrain.clustersZ, handles,
		[](Graphics::TextureHandle h, uint32_t cx, uint32_t cz, uint32_t cid) { return (0x70000000u + cid); },
		/*queryLayer*/nullptr,
		//サンプリングでクラスターの境界に線が発生する問題をとりあえずScaleとOffsetで解決
		{ 0.985f, 0.985f }, { 0.0015f, 0.0015f });

	static Graphics::DX11::SplatArrayResources splatRes;
	Graphics::DX11::InitSplatArrayResources(device, splatRes, terrain.clusters.size());

	BuildSplatArrayFromHandles(device, deviceContext, *textureManager, handles, splatRes);

	// スライステーブルは Array 生成時の uniqueIds から得る
	std::vector<uint32_t> uniqueIds; Graphics::DX11::CollectUniqueSplatIds(terrain, uniqueIds);
	std::unordered_map<uint32_t, int> id2slice = Graphics::DX11::BuildSliceTable(uniqueIds);

	// CPU配列に詰める
	static Graphics::DX11::ClusterParamsGPU cp{};

	Graphics::DX11::FillClusterParamsCPU(terrain, id2slice, cp);

	// グリッドCBを設定（TerrainClustered の定義に合わせて）
	Graphics::DX11::SetupTerrainGridCB(p, /*dimX=*/terrain.clustersX, /*dimZ=*/terrain.clustersZ, cp);

	// 地形のクラスター専用GPUリソースを作る/初期更新
	Graphics::DX11::BuildOrUpdateClusterParamsSB(device, deviceContext, cp);
	Graphics::DX11::BuildOrUpdateTerrainGridCB(device, deviceContext, bufferMgr, cp);

	static Graphics::DX11::CpuImage cpuSplatImage;
	Graphics::DX11::ReadTexture2DToCPU(device, deviceContext, sheetTex.Get(), cpuSplatImage);

	static Graphics::DX11::BlockReservedContext blockRevert;
	ok = blockRevert.Init(graphics.GetDevice(),
		L"assets/shader/CS_TerrainClustered.cso",
		L"assets/shader/CS_TerrainClustered_CSMCombined.cso",
		L"assets/shader/CS_WriteArgs.cso",
		L"assets/shader/CS_WriteArgsShadow.cso",
		L"assets/shader/VS_TerrainClusteredGrid.cso",
		L"assets/shader/VS_TerrainClusteredGridDepth.cso",
		L"assets/shader/PS_TerrainClustered.cso",
		/*maxVisibleIndices*/ (UINT)terrain.indexPool.size());

	assert(ok && "Failed BlockRevert Init");

	Graphics::DX11::BuildFromTerrainClustered(graphics.GetDevice(), terrain, blockRevert);

	static Graphics::TextureHandle heightTexHandle;
	static ComPtr<ID3D11ShaderResourceView> heightMapSRV;

	//HeightMapを16bitテクスチャとして作成
	{
		using namespace Graphics;

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
		auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
		textureMgr->Add(texDesc, heightTexHandle);

		auto texData = textureMgr->Get(heightTexHandle);
		heightMapSRV = texData.ref().srv;
	}

	// ノーマルマップテクスチャの作成
	static ComPtr<ID3D11ShaderResourceView> normalMapSRV;
	{
		using namespace Graphics;

		size_t vertexCount = terrain.vertices.size();
		std::vector<Math::Vec3f> normalMap(vertexCount);
		for(auto i = 0; i < vertexCount; ++i)
		{
			normalMap[i] = terrain.vertices[i].nrm;
		}

		auto normalMapBC5 = EncodeNormalMapBC5(normalMap.data(), (int)(p.cellsX + 1), (int)(p.cellsZ + 1));

		DX11::TextureCreateDesc texDesc;
		DX11::TextureRecipe recipeDesc;
		recipeDesc.width = (p.cellsX + 1) / 4;
		recipeDesc.height = (p.cellsZ + 1) / 4;
		recipeDesc.format = DXGI_FORMAT_BC5_UNORM;
		recipeDesc.usage = D3D11_USAGE_IMMUTABLE;
		recipeDesc.initialData = normalMapBC5.data();
		recipeDesc.initialRowPitch = recipeDesc.width / 4 * 16;
		texDesc.recipe = &recipeDesc;
		auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
		TextureHandle normalTexHandle = {};
		auto texData = textureMgr->CreateResource(texDesc, normalTexHandle);
		normalMapSRV = texData.srv;
	}

	static ComPtr<ID3D11SamplerState> linearSampler;
	{
		using namespace Graphics;
		auto samplerManager = graphics.GetRenderService()->GetResourceManager<DX11::SamplerManager>();

		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		SamplerHandle samp = samplerManager->AddWithDesc(sampDesc);
		auto sampData = samplerManager->Get(samp);
		linearSampler = sampData.ref().state;
	}

	auto terrainUpdateFunc = [](Graphics::RenderService* renderService)
		{
			bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
			if (!execute) return;

			auto viewProj = perCameraService->GetCameraBufferDataNoLock().viewProj;
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
			opt.maxDistance = 200.0f;

			std::vector<uint32_t> clusterIds;
			std::vector<Graphics::SoftTriWorld> trisW;
			std::vector<Graphics::SoftTriClip>  trisC;

			// ---- ハイブリッド抽出 ----
			ExtractOccluderTriangles_HeightmapCoarse_Hybrid(
				terrain, heightTexMap, hopt, opt, clusterIds, trisW, &trisC);

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

	auto PreDrawFunc = [](Graphics::RenderService* renderService, uint32_t slot)
		{
			bool execute = isExecuteCustomFunc.load(std::memory_order_relaxed);
			if (!execute) return;

			auto deviceContext = graphics.GetDeviceContext();

			//auto viewProj = perCameraService->GetCameraBufferData().viewProj;
			auto camPos = perCameraService->GetEyePos();
			Math::Frustumf frustumPlanes
				//Math::Frustumf::MakeFrustumPlanes_WorldSpace_Oriented(viewProj.data(), camPos.data, frustumPlanes.data());
				= perCameraService->MakeFrustum(true);

			auto resolution = perCameraService->GetResolution();
			uint32_t width = (uint32_t)resolution.x;
			uint32_t height = (uint32_t)resolution.y;

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::Default);
			graphics.SetRasterizerState(Graphics::RasterizerStateID::SolidCullBack);

			deviceContext->VSSetSamplers(3, 1, linearSampler.GetAddressOf());

			static Graphics::DX11::BlockReservedContext::ShadowDepthParams shadowParams{};

			shadowParams.mainDSV = graphics.GetMainDepthStencilView().Get();
			shadowParams.mainViewProj = perCameraService->MakeViewProjMatrix();
			memcpy(shadowParams.mainFrustumPlanes, frustumPlanes.data(), sizeof(shadowParams.mainFrustumPlanes));
			auto& cascadeDSV = lightShadowResourceService.GetCascadeDSV();
			for (int c = 0; c < Graphics::kMaxShadowCascades; ++c) {
				shadowParams.cascadeDSV[c] = cascadeDSV[c].Get();
			}

			auto& cascade = lightShadowService.GetCascades();
			memcpy(shadowParams.lightViewProj, cascade.lightViewProj.data(), sizeof(shadowParams.lightViewProj));
			shadowParams.cascadeFrustumPlanes = cascade.frustumWS;

			// フラスタムをライトの逆方向に押し出して影の判定を緩める
			auto pushDir = lightShadowService.GetDirectionalLight().directionWS * -1.0f;
			float lenDot = pushDir.normalized().dot({ 0.0f, 1.0f,0.0f });

			// 垂直になるほど大きくなる
			float pushLen = 200.0f * (1.0f - std::abs(lenDot));

			for (auto& fru : shadowParams.cascadeFrustumPlanes) {
				fru = fru.PushedAlongDirection(pushDir, pushLen);
			}

			shadowParams.screenW = WINDOW_WIDTH;
			shadowParams.screenH = WINDOW_HEIGHT;

			// シャドウマップ用のSRVを解除
			constexpr ID3D11ShaderResourceView* nullSRV = nullptr;
			deviceContext->PSSetShaderResources(7, 1, &nullSRV);

			lightShadowResourceService.ClearDepthBuffer(deviceContext);

			//CBの5, Samplerの1にバインド
			lightShadowResourceService.BindShadowResources(deviceContext, 5);

			auto bufMgr = renderService->GetResourceManager<Graphics::DX11::BufferManager>();
			auto cameraHandle = bufMgr->FindByName(Graphics::DX11::PerCamera3DService::BUFFER_NAME);
			ComPtr<ID3D11Buffer> cameraCB;
			{
				auto cameraBufData = bufMgr->Get(cameraHandle);
				cameraCB = cameraBufData->buffer;
			}

			blockRevert.RunShadowDepth(deviceContext,
				std::move(cameraCB),
				heightMapSRV,
				normalMapSRV,
				shadowParams,
				cp,
				&lightShadowResourceService.GetCascadeViewport(), false);
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

			deviceContext->RSSetViewports(1, &graphics.GetMainViewport());

			blockRevert.RunColor(deviceContext, heightMapSRV, normalMapSRV, cp);
		};

	auto drawFirefly = [](uint64_t frame)
		{
			auto deviceContext = graphics.GetDeviceContext();

			graphics.SetDepthStencilState(Graphics::DepthStencilStateID::DepthReadOnly);

			graphics.SetBlendState(Graphics::BlendStateID::Additive);

			ComPtr<ID3D11ShaderResourceView> heightMapSRV;
			{
				auto heightMapData = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>()->Get(heightTexHandle);
				heightMapSRV = heightMapData.ref().srv;
			}

			//ホタルのスポーン処理
			fireflyService.SpawnParticles(deviceContext, heightMapSRV, cp.cbGrid, frame % Graphics::RENDER_BUFFER_COUNT);
		};

	renderService->SetCustomUpdateFunction(terrainUpdateFunc);
	renderService->SetCustomPreDrawFunction(PreDrawFunc);

	//デバッグ用の初期化
	//========================================================================================-
	using namespace SFW::Graphics;

	// レンダーパイプライン初期化関数
	graphics.ExecuteCustomFunc([&](
		Graphics::DX11::GraphicsDevice::RenderGraph* renderGraph,
		ComPtr<ID3D11RenderTargetView>& mainRenderTarget,
		ComPtr<ID3D11DepthStencilView>& mainDepthStencilView,
		ComPtr<ID3D11ShaderResourceView>& mainDepthStencilSRV)
		{
			InitializeRenderPipeLine(renderGraph, &graphics, mainRenderTarget, mainDepthStencilView, mainDepthStencilSRV,
				drawTerrainColor, drawFirefly, &lightShadowResourceService, deferredRenderingService);
		}
	);

	SFW::World<Grid2DPartition, VoidPartition> world(std::move(serviceLocator));
	auto entityManagerReg = world.GetServiceLocator().Get<SpatialChunkRegistry>();

	auto& worldRequestService = world.GetRequestServiceNoLock();

	// グローバルシステムの追加
	{
		std::vector<std::unique_ptr<decltype(world)::IRequestCommand>> reqCmds;
		reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<CameraSystem>());
		reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<EnvironmentSystem>());
		reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<LightShadowSystem>());

#ifdef _ENABLE_IMGUI
		reqCmds.push_back(worldRequestService.CreateAddGlobalSystemCommand<GlobalDebugRenderSystem>());
#endif

		// レベル追加コマンドを実行キューにプッシュ
		for (auto& cmd : reqCmds) {
			worldRequestService.PushCommand(std::move(cmd));
		}
	}

	{
		auto level = std::unique_ptr<Level<VoidPartition>>(new Level<VoidPartition>("Title", *entityManagerReg, ELevelState::Main));

		auto reqCmd = worldRequestService.CreateAddLevelCommand(std::move(level),
			[&](const ECS::ServiceLocator* serviceLocator, SFW::Level<VoidPartition>* pLevel)
			{
				auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
				auto matMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::MaterialManager>();
				auto shaderMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::ShaderManager>();
				auto psoMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::PSOManager>();
				auto sampMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::SamplerManager>();


				DX11::ShaderCreateDesc shaderDesc;
				shaderDesc.vsPath = L"assets/shader/VS_WindSprite.cso";
				shaderDesc.psPath = L"assets/shader/PS_Color.cso";
				ShaderHandle shaderHandle;
				shaderMgr->Add(shaderDesc, shaderHandle);

				DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
				PSOHandle psoHandle;
				psoMgr->Add(psoDesc, psoHandle);

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
				sprite.pso = psoHandle;
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

				auto& scheduler = pLevel->GetScheduler();
				scheduler.AddSystem<SpriteRenderSystem>(*serviceLocator);

				perCameraService->SetTarget({ 100.0f,-1.0f,100.0f });
				Math::Quatf cameraRot = Math::Quatf::FromAxisAngle({ 1.0f,0.0f,0.0f }, Math::Deg2Rad(-20.0f));
				perCameraService->Rotate(cameraRot);

			});

		// レベル追加コマンドを実行キューにプッシュ
		worldRequestService.PushCommand(std::move(reqCmd));
	}

	static constexpr const char* LoadingLevelName = "Loading";

	{
		auto level = std::unique_ptr<Level<VoidPartition>>(new Level<VoidPartition>(LoadingLevelName, *entityManagerReg, ELevelState::Main));

		auto reqCmd = worldRequestService.CreateAddLevelCommand(std::move(level),
			[&](const ECS::ServiceLocator* serviceLocator, SFW::Level<VoidPartition>* pLevel)
			{
				auto textureMgr = graphics.GetRenderService()->GetResourceManager<DX11::TextureManager>();
				auto matMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::MaterialManager>();
				auto shaderMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::ShaderManager>();
				auto sampMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::SamplerManager>();


				DX11::ShaderCreateDesc shaderDesc;
				shaderDesc.vsPath = L"assets/shader/VS_SpriteAnimation.cso";
				shaderDesc.psPath = L"assets/shader/PS_Color.cso";
				ShaderHandle shaderHandle;
				shaderMgr->Add(shaderDesc, shaderHandle);

				D3D11_SAMPLER_DESC sampDesc = {};
				sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
				SamplerHandle samp = sampMgr->AddWithDesc(sampDesc);

				DX11::TextureCreateDesc textureDesc;
				textureDesc.path = "assets/texture/sprite/ToxicFrogPurpleBlue_Hop.png";
				textureDesc.forceSRGB = true;
				Graphics::TextureHandle texHandle;
				textureMgr->Add(textureDesc, texHandle);

				auto spriteInstBufferHandle= spriteAnimationService.GetInstanceBufferHandle();

				Graphics::DX11::MaterialCreateDesc matDesc;
				matDesc.shader = shaderHandle;
				matDesc.samplerMap[0] = samp;
				matDesc.vsSRV[11] = spriteInstBufferHandle; // VS_CB11 にセット
				matDesc.psSRV[2] = texHandle; // TEX2 にセット

				Graphics::MaterialHandle matHandle;
				matMgr->Add(matDesc, matHandle);

				CSpriteAnimation spriteAnim;
				spriteAnim.hMat = matHandle;
				spriteAnim.buf.divX = 7; // 横分割数

				auto getPos = [](float x, float y)->Math::Vec3f {
					Math::Vec3f pos;
					pos.x = (WINDOW_WIDTH * x) / 2.0f;
					pos.y = (WINDOW_HEIGHT * y) / 2.0f;
					pos.z = 0.0f;
					return pos;
					};

				auto getScale = [](float s)->Math::Vec3f {
					Math::Vec3f scale;
					constexpr auto half = (WINDOW_WIDTH + WINDOW_HEIGHT) / 2.0f;

					scale.x = half * s;
					scale.y = half * s;
					scale.z = 1.0f;
					return scale;
					};

				auto levelSession = pLevel->GetSession();
				levelSession.AddGlobalEntity(
					CTransform{ getPos(0.9f, -0.85f), {0.0f,0.0f,0.0f,1.0f}, getScale(0.15f)},
					spriteAnim);

				auto& scheduler = pLevel->GetScheduler();
				scheduler.AddSystem<SpriteAnimationSystem>(*serviceLocator);

			});

		// レベル追加コマンドを実行キューにプッシュ
		worldRequestService.PushCommand(std::move(reqCmd));
	}

	{
		using OpenFieldLevel = SFW::Level<Grid2DPartition>;

		auto level = std::unique_ptr<OpenFieldLevel>(new OpenFieldLevel("OpenField", *entityManagerReg, ELevelState::Main));

		auto reqCmd = worldRequestService.CreateAddLevelCommand(
			std::move(level),
			//ロード時
			[&](const ECS::ServiceLocator* serviceLocator, OpenFieldLevel* pLevel) {

				auto modelAssetMgr = graphics.GetRenderService()->GetResourceManager<DX11::ModelAssetManager>();

				auto shaderMgr = graphics.GetRenderService()->GetResourceManager<DX11::ShaderManager>();

				clock_t start = clock();


				//デフォルト描画のPSO生成
				DX11::ShaderCreateDesc shaderDesc;
				shaderDesc.templateID = MaterialTemplateID::PBR;
				shaderDesc.vsPath = L"assets/shader/VS_ClipUVNrm.cso";
				shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
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
				shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
				shaderMgr->Add(shaderDesc, shaderHandle);
				PSOHandle windGrassPSOHandle;
				psoDesc.shader = shaderHandle;
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
				psoMgr->Add(psoDesc, windGrassPSOHandle);
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

				shaderDesc.vsPath = L"assets/shader/VS_WindEntity.cso";
				shaderDesc.psPath = L"assets/shader/PS_Opaque.cso";
				shaderMgr->Add(shaderDesc, shaderHandle);
				PSOHandle cullNoneWindEntityPSOHandle;
				psoDesc.shader = shaderHandle;

				shaderDesc.vsPath = L"assets/shader/VS_WindEntityShadow.cso";
				shaderDesc.psPath.clear();// 頂点シェーダのみ
				shaderMgr->Add(shaderDesc, shaderHandle);
				psoDesc.rebindShader = shaderHandle;

				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullNone;
				psoMgr->Add(psoDesc, cullNoneWindEntityPSOHandle);
				psoDesc.rebindShader = std::nullopt;
				psoDesc.rasterizerState = Graphics::RasterizerStateID::SolidCullBack;

				shaderDesc.vsPath = L"assets/shader/VS_NormalMap.cso";
				shaderDesc.psPath = L"assets/shader/PS_NormalMap.cso";
				shaderMgr->Add(shaderDesc, shaderHandle);
				PSOHandle normalMapPSOHandle;
				psoDesc.shader = shaderHandle;
				psoMgr->Add(psoDesc, normalMapPSOHandle);

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
				modelDesc.buildOccluders = false;

				modelAssetMgr->Add(modelDesc, modelAssetHandle[0]);

				modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
				modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

				modelDesc.path = "assets/model/Stylized/YellowFlower.gltf";
				modelDesc.buildOccluders = false;
				modelDesc.viewMax = 50.0f;
				modelDesc.minAreaFrec = 0.0004f;
				modelDesc.pCustomNrmWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.pso = cullNoneWindEntityPSOHandle;

				modelAssetMgr->Add(modelDesc, modelAssetHandle[1]);

				modelDesc.path = "assets/model/Stylized/Tree01.gltf";
				modelDesc.viewMax = 100.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.minAreaFrec = 0.001f;
				modelDesc.pCustomNrmWFunc = WindMovementService::ComputeTreeWeight;
				modelAssetMgr->Add(modelDesc, modelAssetHandle[2]);

				modelDesc.instancesPeak = 100;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.pCustomNrmWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.minAreaFrec = 0.0004f;
				modelDesc.path = "assets/model/Stylized/WhiteCosmos.gltf";
				modelAssetMgr->Add(modelDesc, modelAssetHandle[3]);

				modelDesc.instancesPeak = 100;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = cullNoneWindEntityPSOHandle;
				modelDesc.path = "assets/model/Stylized/YellowCosmos.gltf";
				modelAssetMgr->Add(modelDesc, modelAssetHandle[4]);
				modelDesc.ClearAdditionalBindings();

				ModelAssetHandle playerModelHandle;
				modelDesc.pso = cullDefaultPSOHandle;
				modelDesc.path = "assets/model/BlackGhost.glb";
				modelDesc.pCustomNrmWFunc = nullptr;
				modelDesc.minAreaFrec = 0.001f;
				modelAssetMgr->Add(modelDesc, playerModelHandle);

				ModelAssetHandle grassModelHandle;

				//ディファ―ド用のカメラの定数バッファハンドル取得
				auto deferredCameraHandle = bufferMgr->FindByName(DeferredRenderingService::BUFFER_NAME);

				modelDesc.BindVS_CBV("CameraBuffer", deferredCameraHandle); // カメラCBVをバインド
				modelDesc.BindVS_CBV("TerrainGridCB", cp.gridHandle); // 地形グリッドCBVをバインド
				modelDesc.BindVS_CBV("WindCB", windCBHandle); // 草揺れ用CBVをバインド
				modelDesc.BindVS_CBV("GrassFootCB", footCBHandle); // 草揺れ用CBVをバインド

				modelDesc.BindVS_SRV("gHeightMap", heightTexHandle); // 高さテクスチャをバインド

				modelDesc.instancesPeak = 10000;
				modelDesc.viewMax = 50.0f;
				modelDesc.pso = windGrassPSOHandle;
				modelDesc.pCustomNrmWFunc = WindMovementService::ComputeGrassWeight;
				modelDesc.minAreaFrec = 0.005f;
				modelDesc.path = "assets/model/Stylized/StylizedGrass.gltf";
				bool existingModel = modelAssetMgr->Add(modelDesc, grassModelHandle);
				modelDesc.pCustomNrmWFunc = nullptr;

				// 新規の場合、草のマテリアルに草揺れ用CBVをセット
				if(!existingModel)
				{
					auto data = modelAssetMgr->GetWrite(grassModelHandle);
					auto& submesh = data.ref().subMeshes;

					for (auto& mesh : submesh)
					{
						auto matData = materialMgr->GetWrite(mesh.material);

						//頂点シェーダーにもバインドする設定にする
						matData.ref().isBindVSSampler = true;

						for (auto& tpx : mesh.lodThresholds.Tpx) // LOD調整
						{
							tpx *= 4.0f;
						}
					}
				}

				modelDesc.ClearAdditionalBindings();

				ModelAssetHandle ruinTowerModelHandle;
				modelDesc.instancesPeak = 2;
				modelDesc.viewMax = 1000.0f;
				modelDesc.pso = normalMapPSOHandle;
				modelDesc.minAreaFrec = 0.0f;
				modelDesc.path = "assets/model/Ruins/RuinTower.gltf";
				modelDesc.buildOccluders = true;
				existingModel = modelAssetMgr->Add(modelDesc, ruinTowerModelHandle);

				if(!existingModel && modelDesc.buildOccluders)
				{
					auto ruinTowerData = modelAssetMgr->GetWrite(ruinTowerModelHandle);
					// 遮蔽AABBを少し小さくする
					auto& occAABB = ruinTowerData.ref().subMeshes[0].occluder.meltAABBs[0];
					occAABB.lb.x *= 0.4f;
					occAABB.lb.z *= 0.4f;
					occAABB.ub.x *= 0.4f;
					occAABB.ub.z *= 0.4f;
				}


				ModelAssetHandle ruinBreakTowerModelHandle;
				modelDesc.path = "assets/model/Ruins/RuinBreakTowerA.gltf";
				//中に入るタイプのモデルのオクル―ダーメッシュはまだできていないのでとりあえずfalse
				modelDesc.buildOccluders = false;
				existingModel = modelAssetMgr->Add(modelDesc, ruinBreakTowerModelHandle);


				ModelAssetHandle ruinStoneModelHandle;
				modelDesc.instancesPeak = 10;
				modelDesc.viewMax = 200.0f;
				modelDesc.pso = normalMapPSOHandle;
				modelDesc.path = "assets/model/Ruins/RuinStoneA.gltf";
				modelDesc.rhFlipZ = true; // 右手系GLTF用のZ軸反転フラグを
				modelDesc.buildOccluders = true;
				modelAssetMgr->Add(modelDesc, ruinStoneModelHandle);

				auto ps = serviceLocator->Get<Physics::PhysicsService>();

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


				clock_t end = clock();

				const double time = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000.0;
				printf("create entity time %lf[ms]\n", time);

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

				for (int j = 0; j < (100 * terrainRank); ++j) {
					for (int k = 0; k < (100 * terrainRank); ++k) {
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
							float t = 1.0f - splatR / 255.0f; // 0..1
							constexpr float k = 8.0f;            // カーブの強さ（お好み）

							// 0..1 に正規化した exp カーブ
							float w = (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f); // w: 0..1

							location.y -= w * 4.0f;   // 最大で 4 下げる（0..4）

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

				// 点光源生成
				/*for (int j = 0; j < 10; ++j) {
					for (int k = 0; k < 10; ++k) {
						for (int n = 0; n < 1; ++n) {
							float scaleXZ = 15.0f;
							float scaleY = 15.0f;
							Math::Vec2f offsetXZ = { 12.0f,12.0f };
							Math::Vec3f location = { float(j) * scaleXZ + offsetXZ.x , 0, float(k) * scaleXZ + offsetXZ.y };

							float height = 0.0f;
							terrain.SampleHeightNormalBilinear(location.x, location.z, height);
							location.y = height + 5.0f;

							Graphics::PointLightDesc plDesc;
							plDesc.positionWS = location;
							plDesc.color = { 1.0f,0.8f,0.6f };
							plDesc.intensity = 0.5f;
							plDesc.range = 10.0f;
							plDesc.castsShadow = false;
							auto plHandle = pointLightService.Create(plDesc);

							levelSession.AddEntity(
								CPointLight{ plHandle }
							);
						}
					}
				}*/

				auto getTerrainLocation = [&](float u, float v) {

					Math::Vec3f location = { p.cellsX * p.cellSize * u, 0.0f, p.cellsZ * p.cellSize * v };
					terrain.SampleHeightNormalBilinear(location.x, location.z, location.y);
					return location;
					};

				// Entity生成
				std::uniform_int_distribution<uint32_t> distX(0, (std::numeric_limits< uint32_t>::max)());
				std::uniform_int_distribution<uint32_t> distZ(0, (std::numeric_limits< uint32_t>::max)());


				for (int j = 0; j < (100 * terrainRank); ++j) {
					for (int k = 0; k < (100 * terrainRank); ++k) {
						for (int n = 0; n < 1; ++n) {
							float u = distX(rng) / float((std::numeric_limits< uint32_t>::max)());
							float v = distZ(rng) / float((std::numeric_limits< uint32_t>::max)());
							Math::Vec3f location = getTerrainLocation(u, v);
							//Math::Vec3f location = { 30.0f + j * 10.0f,0.0f, 30.0f + k * 10.0f};

							int modelIdx = dist(rng);
							float scale = modelScaleBase[modelIdx] + float(rand() % modelScaleRange[modelIdx] - modelScaleRange[modelIdx] / 2) / 100.0f;
							//float scale = 1.0f;
							auto rot = Math::Quatf::FromAxisAngle({ 0,1,0 }, Math::Deg2Rad(float(rand() % modelRotRange[modelIdx])));
							auto modelComp = CModel{ modelAssetHandle[modelIdx] };
							modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
							modelComp.flags |= enableOutline[modelIdx] ? (uint16_t)EModelFlag::Outline : (uint16_t)EModelFlag::None;

							if (makeShapeHandleFunc[modelIdx] != nullptr)
							{
								auto chunk = pLevel->GetChunk(location);
								auto key = chunk.value()->GetNodeKey();
								SpatialMotionTag tag{};
								tag.handle = { key, chunk.value() };

								Physics::CPhyBody staticBody{};
								staticBody.type = Physics::BodyType::Static; // staticにする
								staticBody.layer = Physics::Layers::NON_MOVING_RAY_IGNORE;
								auto shapeHandle = makeShapeHandleFunc[modelIdx](Math::Vec3f(scale, scale, scale));
#ifdef _ENABLE_IMGUI
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
#ifdef _ENABLE_IMGUI
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
					Math::Vec3f playerStartPos = getTerrainLocation(0.45f, 0.55f); //中央
					//Math::Vec3f playerStartPos = { 10.0f, 0.0f,  10.0f };

					playerStartPos.y += 10.0f; // 少し浮かせる

					Physics::ShapeCreateDesc shapeDesc;
					shapeDesc.shape = Physics::CapsuleDesc{ 2.0f, 1.0f };
					shapeDesc.localOffset.y += 2.0f;
					auto playerShape = ps->MakeShape(shapeDesc);
#ifdef _ENABLE_IMGUI
					auto playerDims = ps->GetShapeDims(playerShape);
#endif

					CModel modelComp{ playerModelHandle };
					modelComp.flags |= (uint16_t)EModelFlag::CastShadow;
					auto id = levelSession.AddGlobalEntity(
						CTransform{ playerStartPos ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } },
						modelComp,
						PlayerComponent{}
#ifdef _ENABLE_IMGUI
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
					Physics::CPhyBody terrainBody{};
					terrainBody.type = Physics::BodyType::Static; // staticにする
					terrainBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;
					auto id = levelSession.AddEntity(
						CTransform{ 0.0f, -40.0f, 0.0f ,0.0f,0.0f,0.0f,1.0f,1.0f,1.0f,1.0f },
						terrainBody
					);
					if (id) {
						auto chunk = pLevel->GetChunk({ 0.0f, -40.0f, 0.0f }, EOutOfBoundsPolicy::ClampToEdge);
						ps->EnqueueCreateIntent(id.value(), terrainShape, chunk.value()->GetNodeKey());
					}
				}

				// 塔生成
				{
					Math::Vec3f location = getTerrainLocation(0.7f, 0.7f);
					location.y -= 10.0f; // 少し埋める

					auto shape = ps->MakeMesh("generated/meshshape/Ruins/RuinTower.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shape);
#endif
					CModel modelComp{ ruinTowerModelHandle };
					modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

					auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };

					auto id = levelSession.AddGlobalEntity(
						tf,
						modelComp,
						staticBody
#ifdef _ENABLE_IMGUI
						, shapeDims.value()
#endif
					);
					if (id) {
						// チャンクに属さないので直接ボディ作成コマンドを発行
						auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
						ps->CreateBody(bodyCmd);
					}
				}

				// 壊れた塔生成
				{
					Math::Vec3f location = getTerrainLocation(0.4f, 0.62f);
					location.y -= 4.0f; // 少し埋める

					auto shape = ps->MakeMesh("generated/meshshape/Ruins/RuinBreakTowerA.meshbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shape);
#endif
					CModel modelComp{ ruinBreakTowerModelHandle };
					modelComp.flags |= (uint16_t)EModelFlag::CastShadow;

					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;

					auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };

					auto id = levelSession.AddGlobalEntity(
						tf,
						modelComp,
						staticBody
#ifdef _ENABLE_IMGUI
						, shapeDims.value()
#endif
					);
					if (id) {
						// チャンクに属さないので直接ボディ作成コマンドを発行
						auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
						ps->CreateBody(bodyCmd);
					}
				}

				//石碑生成
				{
					Math::Vec3f location = getTerrainLocation(0.3f, 0.3f);
					location.y -= 4.0f; // 少し埋める

					auto shape = ps->MakeConvexCompound("generated/convex/Ruins/RuinStoneA.chullbin", true, Math::Vec3f{ 1.0f,1.0f,1.0f });
#ifdef _ENABLE_IMGUI
					auto shapeDims = ps->GetShapeDims(shape);
#endif
					CModel modelComp{ ruinStoneModelHandle };
					modelComp.flags = (uint16_t)EModelFlag::CastShadow;
					Physics::CPhyBody staticBody{};
					staticBody.type = Physics::BodyType::Static; // staticにする
					staticBody.layer = Physics::Layers::NON_MOVING_RAY_HIT;
					auto tf = CTransform{ location ,{0.0f,0.0f,0.0f,1.0f},{1.0f,1.0f,1.0f } };
					auto id = levelSession.AddGlobalEntity(
						tf,
						modelComp,
						staticBody
#ifdef _ENABLE_IMGUI
						, shapeDims.value()
#endif
					);
					if (id) {
						// チャンクに属さないので直接ボディ作成コマンドを発行
						auto bodyCmd = MakeNoMoveChunkCreateBodyCmd(id.value(), tf, staticBody, shape);
						ps->CreateBody(bodyCmd);
					}
				}

				//蛍の領域生成
				{
					Math::Vec3f location = getTerrainLocation(0.42f, 0.58f);
					//location.y += 5.0f; // 少し浮かせる

					CFireflyVolume fireflyVolume;
					fireflyVolume.centerWS = location;
					fireflyVolume.hitRadius = 40.0f;
					fireflyVolume.spawnRadius = 50.0f;

					//位置を指定して追加
					levelSession.AddEntityWithLocation(fireflyVolume.centerWS, fireflyVolume);
				}


				// System登録
				auto& scheduler = pLevel->GetScheduler();

				scheduler.AddSystem<ModelRenderSystem>(*serviceLocator);

				//scheduler.AddSystem<SimpleModelRenderSystem>(*serviceLocator);
				//scheduler.AddSystem<PhysicsSystem>(*serviceLocator);
				scheduler.AddSystem<BuildBodiesFromIntentsSystem>(*serviceLocator);
				scheduler.AddSystem<BodyIDWriteBackFromEventsSystem>(*serviceLocator);
				scheduler.AddSystem<PlayerSystem>(*serviceLocator);
				scheduler.AddSystem<PointLightSystem>(*serviceLocator);
				scheduler.AddSystem<FireflySystem>(*serviceLocator);
				//scheduler.AddSystem<CleanModelSystem>(*serviceLocator);

#ifdef _ENABLE_IMGUI
				scheduler.AddSystem<DebugRenderSystem>(*serviceLocator);
#endif

				//カスタムの処理を開始
				isExecuteCustomFunc.store(true, std::memory_order_relaxed);

			},
			//アンロード時
			[&](const ECS::ServiceLocator*, OpenFieldLevel* pLevel)
			{
				isExecuteCustomFunc.store(false, std::memory_order_relaxed);
			});

			// レベル追加コマンドを実行キューにプッシュ
			worldRequestService.PushCommand(std::move(reqCmd));
	}

	//初めのレベルをロード
	{
		//ローディング中のレベルを先にロード
		world.LoadLevel("Loading");

		//ロード完了後のコールバック
		auto loadedFunc = [](decltype(world)::Session* pSession) {

			//ローディングレベルをクリーンアップ
			pSession->CleanLevel(LoadingLevelName);
			};

		world.LoadLevel("OpenField", true, true, loadedFunc);
	}

	static GameEngine gameEngine(std::move(graphics), std::move(world), FPS_LIMIT);

	//シーンロードのデバッグコールバック登録
	{
		static std::string loadLevelName;

		BIND_DEBUG_TEXT("Level", "Name", &loadLevelName);

		static bool loadAsync = false;

		BIND_DEBUG_CHECKBOX("Level", "loadAsync", &loadAsync);

		REGISTER_DEBUG_BUTTON("Level", "load", [](bool) {
			auto& worldRequestService = gameEngine.GetWorld().GetRequestServiceNoLock();

			if (loadAsync) {
				//ローディング中のレベルを先にロード
				auto loadingCmd = worldRequestService.CreateLoadLevelCommand(LoadingLevelName, false);
				worldRequestService.PushCommand(std::move(loadingCmd));
			}

			//ロード完了後のコールバック
			auto loadedFunc = [](decltype(world)::Session* pSession) {

				//ローディングレベルをクリーンアップ
				pSession->CleanLevel(LoadingLevelName);
				};

			auto reqCmd = worldRequestService.CreateLoadLevelCommand(loadLevelName, loadAsync, true, loadAsync ? loadedFunc : nullptr);
			worldRequestService.PushCommand(std::move(reqCmd));
			});

		REGISTER_DEBUG_BUTTON("Level", "clean", [](bool) {
			auto& worldRequestService = gameEngine.GetWorld().GetRequestServiceNoLock();
			auto reqCmd = worldRequestService.CreateCleanLevelCommand(loadLevelName);
			worldRequestService.PushCommand(std::move(reqCmd));
			});
	}

	// メッセージループ
	WindowHandler::Run([]() {
		// ここにメインループの処理を書く
		gameEngine.MainLoop(threadPool.get());
		});

	// ワーカースレッドを停止
	threadPool.reset();

	return WindowHandler::Destroy();
}