#include "TerrainWater.h"

#include <SectorFW/Graphics/ImageLoader.h>
#include <SectorFW/Graphics/DX11/DX11BlockRevertHelper.h>

#include "../graphics/D3D11Helpers.h"

#ifdef _DEBUG

float gDebugFlowSpeed0 = 0.02f;
float gDebugFlowSpeed1 = 0.03f;
float gDebugNormalStrength = 1.1f;
float gDebugSpecPower = 20.0f;

float gDebugDepthColorScale = 0.35f;
float gDebugWaterAlpha = 0.92f;
float gDebugShoreFadeScale = 1.5f;

float gDebugFoamDepthStart = 0.02f;
float gDebugFoamDepthEnd = 0.25f;
float gDebugFoamIntensity = 0.8f;
float gDebugFoamNoiseScale = 6.0f;

#endif

void TerrainWater::BuildCluster(BuilderParams& p)
{
	//本当はよくないがいったん確認のためにここで定義
#ifdef _DEBUG

	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FlowSpeed1", &gDebugFlowSpeed0, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FlowSpeed2", &gDebugFlowSpeed1, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "NormalStrength", &gDebugNormalStrength, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "SpecPower", &gDebugSpecPower, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "DepthColorScale", &gDebugDepthColorScale, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "WaterAlpha", &gDebugWaterAlpha, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "ShoreFadeScale", &gDebugShoreFadeScale, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FoamDepthStart", &gDebugFoamDepthStart, 0.0f, 1.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FoamDepthEnd", &gDebugFoamDepthEnd, 0.0f, 10.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FoamIntensity", &gDebugFoamIntensity, 0.0f, 5.0f, 0.01f);
	BIND_DEBUG_SLIDER_FLOAT("TerrainWater", "FoamNoiseScale", &gDebugFoamNoiseScale, 0.0f, 20.0f, 0.1f);

#endif

	if (p.clusterCellsX == 0 || p.clusterCellsZ == 0)
	{
		LOG_ERROR("Invalid cluster cell count: clusterCellsX={}, clusterCellsZ={}", p.clusterCellsX, p.clusterCellsZ);
		return;
	}

	params = p;

	auto imgData = Graphics::LoadImageFromFile(p.heigthMapPath, 1, // 1 チャネルで読み込む（グレースケール）
		false
	);

	float clusterWorldSizeX = p.clusterCellsX * p.cellSize;
	float clusterWorldSizeZ = p.clusterCellsZ * p.cellSize;

	// クラスター数を計算
	int cx = static_cast<int>(std::ceil(p.worldMapSizeX / clusterWorldSizeX));
	int cz = static_cast<int>(std::ceil(p.worldMapSizeZ / clusterWorldSizeZ));

	if (cx <= 0 || cz <= 0)
	{
		LOG_ERROR("Invalid cluster count: clustersX={}, clustersZ={}", cx, cz);
		return;
	}

	clustersX = static_cast<uint32_t>(cx);
	clustersZ = static_cast<uint32_t>(cz);

	constexpr float HEIGHT_CLUSTER_THRESHOLD = 0.1f; // 水面とみなす高さの閾値（0.0 - 1.0）

	// クラスターごとに平均高さを計算し、閾値以上なら水面クラスターとして登録
	for (int z = 0; z < cz; ++z)
	{
		for (int x = 0; x < cx; ++x)
		{
			int startX = static_cast<int>(x / static_cast<float>(cx) * imgData.width);
			int startZ = static_cast<int>(z / static_cast<float>(cz) * imgData.height);
			int endX = static_cast<int>((x + 1) / static_cast<float>(cx) * imgData.width);
			int endZ = static_cast<int>((z + 1) / static_cast<float>(cz) * imgData.height);

			float maxHeight = 0.0f;
			float minHeight = 1.0f;

			for (int iz = startZ; iz < endZ; ++iz)
			{
				for (int ix = startX; ix < endX; ++ix)
				{
					int idx = iz * imgData.width + ix;
					uint8_t heightValue = (uint8_t)imgData.pixels.get()[idx];
					float height = static_cast<float>(heightValue) / 255.0f; // 0.0 - 1.0 に正規化
					maxHeight = (std::max)(maxHeight, height);
					minHeight = (std::min)(minHeight, height);
				}
			}

			// 最大高さが閾値以下なら水面クラスターとみなさない
			if (maxHeight < HEIGHT_CLUSTER_THRESHOLD) continue;

			waterClusters.push_back({ (uint32_t)x, (uint32_t)z });

			// クラスターのワールド座標範囲を計算して AABB を作成
			// XZはクラスターの位置から計算、Yは最大高さに基づいて設定

			constexpr float HEIGHT_PADDING = 0.4f; // 水面の上下に少し余裕を持たせる

			// 高さの差が小さい場合は、最低でも一定の高さを確保するためにパディングを追加
			float padding = 0.0f;
			float disHeight = (maxHeight - minHeight);
			if (disHeight < HEIGHT_PADDING * 2.0f)
			{
				padding = HEIGHT_PADDING;
			}

			Math::AABB3f bounds;
			bounds.lb.x = x * clusterWorldSizeX + p.worldOffset.x;
			bounds.lb.y = p.worldOffset.y + (minHeight - padding) * p.heightScale;
			bounds.lb.z = z * clusterWorldSizeZ + p.worldOffset.z;
			bounds.ub.x = (x + 1) * clusterWorldSizeX + p.worldOffset.x;
			bounds.ub.y = p.worldOffset.y + (maxHeight + padding) * p.heightScale;
			bounds.ub.z = (z + 1) * clusterWorldSizeZ + p.worldOffset.z;

			waterClusterBounds.push_back(bounds);
		}
	}
}

bool TerrainWater::CompileShader(ID3D11Device* dev, const wchar_t* csBuildPath, const wchar_t* csArgPath, const wchar_t* vsPath, const wchar_t* psPath)
{
	HRESULT hr;

	// Compile/load shaders
	ComPtr<ID3DBlob> blob;
	hr = D3DReadFileToBlob(csBuildPath, blob.GetAddressOf()); if (FAILED(hr)) return false;
	hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &csBuild);
	if (FAILED(hr)) return false;

	blob.Reset();
	hr = D3DReadFileToBlob(csArgPath, blob.GetAddressOf()); if (FAILED(hr)) return false;
	hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &csArg);
	if (FAILED(hr)) return false;

	blob.Reset();
	hr = D3DReadFileToBlob(vsPath, blob.GetAddressOf()); if (FAILED(hr)) return false;
	hr = dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs);
	if (FAILED(hr)) return false;

	blob.Reset();
	hr = D3DReadFileToBlob(psPath, blob.GetAddressOf()); if (FAILED(hr)) return false;
	hr = dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
	if (FAILED(hr)) return false;

	return true;
}

bool TerrainWater::CreateResource(SFW::Graphics::DX11::GraphicsDevice& graphics)
{
	auto* dev = graphics.GetDevice();
	auto* texMgr = graphics.GetRenderService()->GetResourceManager<Graphics::DX11::TextureManager>();

	Graphics::DX11::TextureCreateDesc texDesc{};
	texDesc.path = params.heigthMapPath;
	Graphics::TextureHandle heightMapHandle = {};
	auto texData = texMgr->CreateResource(texDesc, heightMapHandle);
	heightMapSRV = texData.srv;

	texDesc.path = params.normal1Path;
	Graphics::TextureHandle normalMapHandle = {};
	auto normalTexData = texMgr->CreateResource(texDesc, normalMapHandle);
	normal1SRV = normalTexData.srv;

	texDesc.path = params.normal2Path;
	Graphics::TextureHandle normal2MapHandle = {};
	auto normal2TexData = texMgr->CreateResource(texDesc, normal2MapHandle);
	normal2SRV = normal2TexData.srv;

	Graphics::DX11::CreateRawUAV(dev, 16, argsUAVBuf, argsUAV);

	// Indirect args (16B)
	if (!Graphics::DX11::CreateIndirectArgs(dev, argsBuf, 16)) return false;

	std::vector<Math::Vec3f> aabbMins, aabbMaxs;
	for (const auto& bounds : waterClusterBounds)
	{
		aabbMins.push_back(bounds.lb);
		aabbMaxs.push_back(bounds.ub);
	}

	auto minAabbSRVUAV = CreateStructuredBufferSRVUAV(dev, sizeof(Math::Vec3f), (uint32_t)waterClusterBounds.size(), true, false, 0, D3D11_USAGE_DEFAULT, 0, aabbMins.data());
	auto maxAabbSRVUAV = CreateStructuredBufferSRVUAV(dev, sizeof(Math::Vec3f), (uint32_t)waterClusterBounds.size(), true, false, 0, D3D11_USAGE_DEFAULT, 0, aabbMaxs.data());
	clusterAabbMinSRV = minAabbSRVUAV.srv;
	clusterAabbMaxSRV = maxAabbSRVUAV.srv;

	struct GridRect
	{
		uint32_t startX;
		uint32_t startZ;
	};

	std::vector<GridRect> gridRects;
	for (const auto& node : waterClusters)
	{
		gridRects.push_back(GridRect{ node.clusterX * params.clusterCellsX, node.clusterZ * params.clusterCellsZ });
	}

	auto gridRectSRVUAV = CreateStructuredBufferSRVUAV(dev, sizeof(GridRect), (uint32_t)waterClusters.size(), true, false, 0, D3D11_USAGE_DEFAULT, 0, gridRects.data());
	clusterGridRectSRV = gridRectSRVUAV.srv;

	auto counterSRVUAV = CreateRawBufferSRVUAV(dev, sizeof(uint32_t), D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, true, true);
	counterUAV = counterSRVUAV.uav;

	uint32_t maxIndexCount = params.clusterCellsX * params.clusterCellsZ * 6 * (uint32_t)waterClusters.size(); // クラスター1つあたりの最大インデックス数（6は1セルあたりのインデックス数）

	auto indexSRVUAV = CreateStructuredBufferSRVUAV(dev, sizeof(uint32_t), maxIndexCount, true, true, 0, D3D11_USAGE_DEFAULT, 0);
	indexSRV = indexSRVUAV.srv;
	indexUAV = indexSRVUAV.uav;

	auto makeCB = [&](UINT bytes, ComPtr<ID3D11Buffer>& cb, const void* initData = nullptr) {
		D3D11_BUFFER_DESC d{};
		d.ByteWidth = (bytes + 15) & ~15u;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		D3D11_SUBRESOURCE_DATA sd{};
		sd.pSysMem = initData;

		return SUCCEEDED(dev->CreateBuffer(&d, initData ? &sd : nullptr, &cb));
		};

	// 定数バッファ
	if (!makeCB(sizeof(CSParam), cbParam)) return false;

	GridCB gridCbData{};
	gridCbData.gOrigin = params.worldOffset;
	gridCbData.heightScale = params.heightScale;
	gridCbData.gVertsX = clustersX * params.clusterCellsX + 1; // 頂点数はセル数+1
	gridCbData.gVertsZ = clustersZ * params.clusterCellsZ + 1;
	gridCbData.gDimX = clustersX;
	gridCbData.gDimZ = clustersZ;
	gridCbData.gClusterCellsX = params.clusterCellsX;
	gridCbData.gClusterCellsZ = params.clusterCellsZ;
	gridCbData.gCellInvCount[0] = 1.0f / (params.clusterCellsX - 1);
	gridCbData.gCellInvCount[1] = 1.0f / (params.clusterCellsZ - 1);
	gridCbData.gCellSize[0] = gridCbData.gCellSize[1] = params.cellSize;
	gridCbData.gHeightMapInvSize[0] = 1.0f / (gridCbData.gVertsX - 1);
	gridCbData.gHeightMapInvSize[1] = 1.0f / (gridCbData.gVertsZ - 1);

	if (!makeCB(sizeof(GridCB), cbGrid, &gridCbData)) return false;

	if (!makeCB(sizeof(FrameCB), cbFrame, &frameCBData)) return false;

	//wrapサンプラー生成
	D3D11_SAMPLER_DESC sampDesc{};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.MipLODBias = 0.0f;
	sampDesc.MaxAnisotropy = 1;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampDesc.BorderColor[0] = 0.0f;
	sampDesc.BorderColor[1] = 0.0f;
	sampDesc.BorderColor[2] = 0.0f;
	sampDesc.BorderColor[3] = 0.0f;
	sampDesc.MinLOD = 0.0f;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(dev->CreateSamplerState(&sampDesc, wrapSampler.GetAddressOf()))) return false;

	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	if (FAILED(dev->CreateSamplerState(&sampDesc, pointSampler.GetAddressOf()))) return false;

	return true;
}

void TerrainWater::ComputeVisibleIndices(ComputeContext& ctx, ComPtr<ID3D11Buffer> cbCamera)
{
	cbCameraFrame = std::move(cbCamera); // カメラの定数バッファを一時的に借りる

	HRESULT hr;

	D3D11_MAPPED_SUBRESOURCE ms{};
	hr = ctx.devCtx->Map(cbParam.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

	if (SUCCEEDED(hr))
	{
		auto* csp = reinterpret_cast<CSParam*>(ms.pData);
		csp->MainFrustum = ctx.mainFrustum;
		csp->ViewProj = ctx.viewProj;
		csp->MaxVisibleIndices = (params.clusterCellsX * params.clusterCellsZ * 6) * (uint32_t)waterClusters.size();
		csp->LodLevels = 4; // 固定で4レベル（LOD0..LOD3）
		csp->ScreenSize[0] = ctx.screenSize[0];
		csp->ScreenSize[1] = ctx.screenSize[1];

		// LOD しきい値（px）
		csp->LodPxThreshold_Main[0] = 200.0f;
		csp->LodPxThreshold_Main[1] = 100.0f;
		csp->LodPxThreshold_Main[2] = 50.0f;
		csp->LodPxThreshold_Main[3] = 25.0f;

		csp->gVertsX = clustersX * params.clusterCellsX + 1;
		csp->gVertsZ = clustersZ * params.clusterCellsZ + 1;

		csp->gCellsX = params.clusterCellsX;
		csp->gCellsZ = params.clusterCellsZ;

		ctx.devCtx->Unmap(cbParam.Get(), 0);
	}

	// 0) カウンタをクリア
	{
		static constexpr UINT zeros[5u] = { 0 };
		ctx.devCtx->ClearUnorderedAccessViewUint(counterUAV.Get(), zeros);
	}

	// 1) ComputeShader をセットして UAV をバインド
	{
		ctx.devCtx->CSSetShader(csBuild.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[3] = {
			clusterAabbMinSRV.Get(),
			clusterAabbMaxSRV.Get(),
			clusterGridRectSRV.Get(),
		};
		ctx.devCtx->CSSetShaderResources(2, 3, srvs);
		ID3D11UnorderedAccessView* uavs[2] = {
			counterUAV.Get(),
			indexUAV.Get()
		};
		UINT initial[2] = {
			0xFFFFFFFF, 0xFFFFFFFF
		};
		ctx.devCtx->CSSetUnorderedAccessViews(0, 2, uavs, initial);
		ctx.devCtx->CSSetConstantBuffers(4, 1, cbParam.GetAddressOf());
		uint32_t groupCountX = (uint32_t)waterClusters.size();
		ctx.devCtx->Dispatch(groupCountX, 1, 1);
	}

	// 2) CS_WriteArgs で
	//    - counterUAV → argsUAVBuf（メイン）
	//    - cascadeCountersUAV → shadowArgsUAVBuf（シャドウ）
	//    を２回呼んで生成

	{
		ctx.devCtx->CSSetShader(csArg.Get(), nullptr, 0);

		// メイン用 (counterUAV → argsUAV)
		{
			ID3D11UnorderedAccessView* uavsArgs[2] = {
				counterUAV.Get(),        // Counter
				argsUAV.Get()            // ArgsUAV (メイン用)
			};
			UINT initCounts[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
			ctx.devCtx->CSSetUnorderedAccessViews(0, 2, uavsArgs, initCounts);

			ctx.devCtx->Dispatch(1, 1, 1);

			constexpr ID3D11UnorderedAccessView* nullU[2] = { nullptr,nullptr };
			UINT zeroI[2] = { 0,0 };
			ctx.devCtx->CSSetUnorderedAccessViews(0, 2, nullU, zeroI);
		}

		ctx.devCtx->CSSetShader(nullptr, nullptr, 0);
	}

	// 3) ArgsUAV → DrawIndirectArgs にコピー
	{
		ctx.devCtx->CopyResource(argsBuf.Get(), argsUAVBuf.Get());       // メイン
	}
}

void TerrainWater::Render(ID3D11DeviceContext* devCtx,
	ComPtr<ID3D11Buffer> globalLightCB,
	ComPtr<ID3D11ShaderResourceView> depthSRV,
	Math::Matrix4x4f invProj,
	Math::Vec3f camPos,
	float deltaTime,
	float fogStart,
	float fogEnd)
{
	D3D11_MAPPED_SUBRESOURCE ms{};
	HRESULT hr = devCtx->Map(cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

	if (SUCCEEDED(hr))
	{
		auto* frameCB = reinterpret_cast<FrameCB*>(ms.pData);
		frameCBData.gTime += deltaTime;
		frameCBData.gInvProj = invProj;
		frameCBData.gCameraPosWS = camPos;

		frameCBData.fogStart = fogStart;
		frameCBData.fogEnd = fogEnd;

#ifdef _DEBUG
		frameCBData.gFlowSpeed0 = gDebugFlowSpeed0;
		frameCBData.gFlowSpeed1 = gDebugFlowSpeed1;
		frameCBData.gNormalStrength = gDebugNormalStrength;
		frameCBData.gSpecPower = gDebugSpecPower;
		frameCBData.depthColorScale = gDebugDepthColorScale;
		frameCBData.waterAlpha = gDebugWaterAlpha;
		frameCBData.shoreFadeScale = gDebugShoreFadeScale;
		frameCBData.gFoamDepthStart = gDebugFoamDepthStart;
		frameCBData.gFoamDepthEnd = gDebugFoamDepthEnd;
		frameCBData.gFoamIntensity = gDebugFoamIntensity;
		frameCBData.gFoamNoiseScale = gDebugFoamNoiseScale;
#endif

		* frameCB = frameCBData;
		devCtx->Unmap(cbFrame.Get(), 0);
	}

	devCtx->VSSetShader(vs.Get(), nullptr, 0);
	devCtx->PSSetShader(ps.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srvs[2] = {
		indexSRV.Get(),
		heightMapSRV.Get(),
	};
	devCtx->VSSetShaderResources(20, 2, srvs);

	ID3D11ShaderResourceView* psSrvs[3] = {
		normal1SRV.Get(),
		normal2SRV.Get(),
		depthSRV.Get(),
	};

	devCtx->PSSetShaderResources(20, 3, psSrvs);

	ID3D11Buffer* cbs[2] = {
		cbGrid.Get(),
		cbCameraFrame.Get(),
	};
	devCtx->VSSetConstantBuffers(10, 2, cbs);

	ID3D11Buffer* psCBs[2] = {
		globalLightCB.Get(),
		cbFrame.Get(),
	};

	devCtx->PSSetConstantBuffers(8, 2, psCBs);

	devCtx->PSSetSamplers(3, 1, wrapSampler.GetAddressOf());
	devCtx->PSSetSamplers(4, 1, pointSampler.GetAddressOf());

	devCtx->IASetInputLayout(nullptr);
	devCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ここでは Arg を流用するだけで、CS は呼ばない
	devCtx->DrawInstancedIndirect(argsBuf.Get(), 0);

	ID3D11ShaderResourceView* nullSRV[3] = { nullptr,nullptr,nullptr };
	devCtx->VSSetShaderResources(20, 2, nullSRV);
	devCtx->PSSetShaderResources(20, 3, nullSRV);

	cbCameraFrame.Reset();
}
