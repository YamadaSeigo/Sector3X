#include "RenderPipeline.h"
#include "app/AppContext.h"
#include "app/appconfig.h"

#include "RenderDefine.h"
#include "DeferredRenderingService.h"
#include "environment/EnvironmentService.h"
#include "environment/FireflyService.h"
#include "graphics/DebugRenderType.h"

#include <SectorFW/Math/Vector.hpp>
#include <SectorFW/Graphics/LightShadowService.h>
#include <SectorFW/Graphics/DX11/DX11LightShadowResourceService.h>
#include <SectorFW/Util/convert_string.h>
#include <SectorFW/Debug/message.h>


#include <vector>

struct RtPack
{
	std::vector<ComPtr<ID3D11RenderTargetView>> rtv;
	std::vector<ComPtr<ID3D11ShaderResourceView>> srv;
};

bool CreateMRT(ID3D11Device* dev, SFW::Graphics::DX11::TextureManager* texMgr, const DeferredRenderingService& deferredService, RtPack& out)
{
	using namespace SFW::Graphics;

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


void RenderPipe::Initialize(SFW::Graphics::DX11::GraphicsDevice::RenderGraph* renderGraph, App::Context& ctx, ComPtr<ID3D11RenderTargetView>& mainRTV, ComPtr<ID3D11DepthStencilView>& mainDSV, ComPtr<ID3D11DepthStencilView>& mainDSVReadOnly, ComPtr<ID3D11ShaderResourceView>& mainDepthSRV, SFW::Graphics::PassCustomFuncType drawTerrainColor, SFW::Graphics::PassCustomFuncType drawParticle)
{
	using namespace SFW::Graphics;

	auto bufferMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::BufferManager>();
	auto cameraHandle3D = bufferMgr->FindByName(DX11::PerCamera3DService::BUFFER_NAME);
	auto cameraHandle2D = bufferMgr->FindByName(DX11::Camera2DService::BUFFER_NAME);

	auto shaderMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::ShaderManager>();
	auto psoMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::PSOManager>();
	auto textureMgr = renderGraph->GetRenderService()->GetResourceManager<DX11::TextureManager>();

	static RtPack ttMRT;
	bool ok = CreateMRT(ctx.graphics->GetDevice(), textureMgr, *ctx.deferred, ttMRT);
	assert(ok);

	std::vector<ComPtr<ID3D11RenderTargetView>> mainRtv = { mainRTV };

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

	const auto& cascadeDSVs = ctx.shadowRes->GetCascadeDSV();

	auto cascadeCount = cascadeDSVs.size();

	DX11::BufferCreateDesc cbDesc;
	cbDesc.size = sizeof(CascadeIndex);

	Viewport vp;
	vp.width = (float)App::SHADOW_MAP_SIZE;
	vp.height = (float)App::SHADOW_MAP_SIZE;

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

	vp.width = (float)App::WINDOW_WIDTH;
	vp.height = (float)App::WINDOW_HEIGHT;
	passDesc.viewport = vp;

	passDesc.dsv = mainDSV;
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

	static auto gGraphics = ctx.graphics;
	static auto renderBackend = ctx.graphics->GetBackend();
	static auto lightShadowService = ctx.shadowRes;
	static auto fireflyService = ctx.firefly;

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
	defferedShaderDesc.psPath = L"assets/shader/PS_Fullscreen_Unlit_Shadow.cso";
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
	derreredSRVs.push_back(mainDepthSRV.Get());

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
		skyboxData.time += (float)(1.0f / App::FPS_LIMIT);
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

		if (outH) {
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
		hr = ctx.graphics->GetDevice()->CreateRenderTargetView(texData.resource.Get(), &rtvDesc, outRTV.GetAddressOf());
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
			hr = ctx.graphics->GetDevice()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, outPS.GetAddressOf());
			assert(SUCCEEDED(hr) && "Failed to create pixel shader.");
		};

	//　解像度を落とすことでにじみ表現と負荷軽減
	constexpr uint32_t BLOOM_TEX_WIDTH = App::WINDOW_WIDTH / 2;
	constexpr uint32_t BLOOM_TEX_HEIGHT = App::WINDOW_HEIGHT / 2;

	static ComPtr<ID3D11ShaderResourceView> sceneColorSRV;
	static ComPtr<ID3D11RenderTargetView> sceneColorRTV;
	// 1.0以上でも潰れないフォーマット
	createScreenTex(sceneColorSRV, sceneColorRTV, DXGI_FORMAT_R11G11B10_FLOAT, App::WINDOW_WIDTH, App::WINDOW_HEIGHT);

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
		SFW::Math::Vec2f gTexelSize; // 1/width, 1/height
		SFW::Math::Vec2f _pad;
	};

	static BlurCB cpuBlurData = { SFW::Math::Vec2f(1.0f / BLOOM_TEX_WIDTH, 1.0f / BLOOM_TEX_HEIGHT), SFW::Math::Vec2f(0.0f, 0.0f) };
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

		ID3D11ShaderResourceView* fireflyPointLiehgt = fireflyService->GetPointLightSRV();
		ctx->PSSetShaderResources(16, 1, &fireflyPointLiehgt);

		ID3D11Buffer* fireflyLightCountBuf = fireflyService->GetLightCountBuffer();

		D3D11_MAPPED_SUBRESOURCE mapped{};
		HRESULT hr = ctx->Map(fireflyLightCountBuf, 0, D3D11_MAP_READ, 0, &mapped);
		uint32_t fireflyCount = *reinterpret_cast<const uint32_t*>(mapped.pData);
		ctx->Unmap(fireflyLightCountBuf, 0);

		ID3D11Buffer* lightDataBuffer = lightShadowService->GetLightDataCB().Get();
		hr = ctx->Map(lightDataBuffer, 0, D3D11_MAP_READ, 0, &mapped);
		auto& lightCBData = *reinterpret_cast<Graphics::CPULightData*>(mapped.pData);
		lightCBData.gFireflyLightCount = fireflyCount;
		ctx->Unmap(lightDataBuffer, 0);

		renderBackend->BindPSCBVs({ SkyCBBuffer.Get(), invCameraBuffer.Get(), lightDataBuffer, fogBuffer.Get(), godRayBuffer.Get() }, 9);

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
	passDesc.dsv = mainDSV;
	passDesc.cbvs = { BindSlotBuffer{cameraHandle3D} };
	passDesc.psoOverride = std::nullopt;
	passDesc.viewport = vp;
	passDesc.depthStencilState = DepthStencilStateID::Default_Stencil;
	passDesc.customExecute = { drawTerrainColor };
	passDesc.stencilRef = 1;

	renderGraph->AddPassToGroup(main3DGroup, passDesc, PASS_3DMAIN_OUTLINE);

	passDesc.customExecute = { drawSky, drawParticle, drawFullScreen };
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
