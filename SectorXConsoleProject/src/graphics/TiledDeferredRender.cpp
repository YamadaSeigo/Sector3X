#include "TiledDeferredRender.h"
#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>

void TiledDeferredRender::Create(ID3D11Device* dev,
    uint32_t screenWidth,
    uint32_t screenHeight,
    const wchar_t* csBuildFrustum,
    const wchar_t* csTileCulling,
    const wchar_t* csDrawTileLight)
{
    static_assert(TILE_SIZE > 0);

    if (screenWidth % TILE_SIZE != 0 || screenHeight % TILE_SIZE != 0) {
        LOG_WARNING("TiledDeferredRender: Screen size is not multiple of tile size. "
			"Tiles will cover the entire screen, but some tiles may be partially outside the screen.");
    }

    m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;

    m_tilesX = (screenWidth + TILE_SIZE - 1) / TILE_SIZE;
    m_tilesY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;

	m_tileFrustums = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(TileFrustum),
        m_tilesX * m_tilesY,
        true, true,
        0,
        D3D11_USAGE_DEFAULT,
        0);

    m_tileLightIndices = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        m_tilesX * m_tilesY * MAX_LIGHTS_PER_TILE,
        true, true,
        0,
        D3D11_USAGE_DEFAULT,
        0);


    m_lightIndexCounter = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(uint32_t),
        m_tilesX * m_tilesY,
        true, true,
        D3D11_BUFFER_UAV_FLAG_COUNTER,
        D3D11_USAGE_DEFAULT,
        0);

	// Create constant buffer for frustum parameters
    {
        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = sizeof(TileCB);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		TileCB tileCBData{};
		tileCBData.gScreenWidth = m_screenWidth;
		tileCBData.gScreenHeight = m_screenHeight;
		tileCBData.gTilesX = m_tilesX;
		tileCBData.gTilesY = m_tilesY;
		D3D11_SUBRESOURCE_DATA initData{};
		initData.pSysMem = &tileCBData;

        HRESULT hr = dev->CreateBuffer(&cbd, &initData, m_tileCB.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create frustum constant buffer.");
	}

    auto compileShader = [&](const wchar_t* path, ComPtr<ID3D11ComputeShader>& outCS)
        {
            ComPtr<ID3DBlob> csBlob;
            HRESULT hr = D3DReadFileToBlob(path, csBlob.GetAddressOf());
#ifdef _DEBUG
            std::string msgPath = SFW::WCharToUtf8_portable(path);
            DYNAMIC_ASSERT_MESSAGE(SUCCEEDED(hr), "Failed to load compute shader file. {%s}", msgPath.c_str());
#endif
            hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &outCS);
            assert(SUCCEEDED(hr) && "Failed to create compute shader.");
        };

	compileShader(csBuildFrustum, m_csBuildFrustums);
    compileShader(csTileCulling, m_csTileCulling);
    compileShader(csDrawTileLight, m_csDrawTileLight);

}


void TiledDeferredRender::BuildTileFrustums(ID3D11DeviceContext* ctx, ID3D11Buffer* camCB)
{
    ctx->CSSetConstantBuffers(0, 1, m_tileCB.GetAddressOf());
	ctx->CSSetConstantBuffers(1, 1, &camCB);

    ID3D11UnorderedAccessView* uavs[] = { m_tileFrustums.uav.Get() };
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ctx->CSSetShader(m_csBuildFrustums.Get(), nullptr, 0);
    uint32_t groupX = (m_tilesX + (BUILD_FRUSTUM_BLOCK_X - 1)) / BUILD_FRUSTUM_BLOCK_X;
    uint32_t groupY = (m_tilesY + (BUILD_FRUSTUM_BLOCK_Y - 1)) / BUILD_FRUSTUM_BLOCK_Y;
    ctx->Dispatch(groupX, groupY, 1);
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
}

void TiledDeferredRender::TileCullingLight(ID3D11DeviceContext* ctx,
    ID3D11ShaderResourceView* normalLightSRV,
    ID3D11ShaderResourceView* fireflyLightSRV,
    ID3D11ShaderResourceView* depthSRV,
    ID3D11Buffer* camCB,
    ID3D11Buffer* lightCountCB)
{
    ID3D11ShaderResourceView* srvs[] =
    {
        normalLightSRV,
        fireflyLightSRV,
        m_tileFrustums.srv.Get(),
        depthSRV
    };

	constexpr auto srvCount = _countof(srvs);
	ctx->CSSetShaderResources(0, srvCount, srvs);

    ID3D11UnorderedAccessView* uavs[] =
    {
        m_lightIndexCounter.uav.Get(),
        m_tileLightIndices.uav.Get(),
	};

	constexpr auto uavCount = _countof(uavs);
    ctx->CSSetUnorderedAccessViews(0, uavCount, uavs, nullptr);

    ID3D11Buffer* cbs[] = {
        m_tileCB.Get(),
        camCB,
        lightCountCB
    };
	ctx->CSSetConstantBuffers(0, _countof(cbs), cbs);

    ctx->CSSetShader(m_csTileCulling.Get(), nullptr, 0);
    ctx->Dispatch(m_tilesX, m_tilesY, 1);
    ID3D11ShaderResourceView* nullSRVs[srvCount] = { nullptr };
    ctx->CSSetShaderResources(0, srvCount, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[uavCount] = { nullptr };
	ctx->CSSetUnorderedAccessViews(0, uavCount, nullUAVs, nullptr);

}

void TiledDeferredRender::DrawTileLight(ID3D11DeviceContext* ctx,
    ID3D11ShaderResourceView* normalLightSRV,
    ID3D11ShaderResourceView* fireflyLightSRV,
    ID3D11ShaderResourceView* albedoSRV,
    ID3D11ShaderResourceView* normalSRV,
    ID3D11ShaderResourceView* depthSRV,
    ID3D11UnorderedAccessView* outLightTex,
    ID3D11SamplerState* pointSampler,
    ID3D11Buffer* camCB)
{
    ID3D11ShaderResourceView* srvs[] =
    {
        normalLightSRV,
        fireflyLightSRV,
        m_lightIndexCounter.srv.Get(),
        m_tileLightIndices.srv.Get(),
        albedoSRV,
        normalSRV,
        depthSRV,
	};

    constexpr auto srvCount = _countof(srvs);
	ctx->CSSetShaderResources(0, srvCount, srvs);

	ctx->CSSetUnorderedAccessViews(0, 1, &outLightTex, nullptr);

	ctx->CSSetSamplers(0, 1, &pointSampler);

    ID3D11Buffer* cbs[] = {
        m_tileCB.Get(),
        camCB
	};
	ctx->CSSetConstantBuffers(0, _countof(cbs), cbs);

    ctx->CSSetShader(m_csDrawTileLight.Get(), nullptr, 0);
    ctx->Dispatch(m_tilesX, m_tilesY, 1);
    ID3D11ShaderResourceView* nullSRVs[srvCount] = { nullptr };
    ctx->CSSetShaderResources(0, srvCount, nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

}
