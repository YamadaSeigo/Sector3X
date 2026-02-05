#include "TiledDeferredRender.h"
#include <SectorFW/Debug/message.h>
#include <SectorFW/Util/convert_string.h>

void TiledDeferredRender::Create(ID3D11Device* dev,
    uint32_t screenWidth,
    uint32_t screenHeight,
    uint32_t tileSize,
    const wchar_t* csBuildFrustum)
{
    assert(tileSize > 0);

    if (screenWidth % tileSize != 0 || screenHeight % tileSize != 0) {
        LOG_WARNING("TiledDeferredRender: Screen size is not multiple of tile size. "
			"Tiles will cover the entire screen, but some tiles may be partially outside the screen.");
    }

    m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;

    m_tilesX = (screenWidth + tileSize - 1) / tileSize;
    m_tilesY = (screenHeight + tileSize - 1) / tileSize;

	m_tileFrustums = CreateStructuredBufferSRVUAV(
        dev,
        sizeof(TileFrustum),
        m_tilesX * m_tilesY,
        true, true,
        0,
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
