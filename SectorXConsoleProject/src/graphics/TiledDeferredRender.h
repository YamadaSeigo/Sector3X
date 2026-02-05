#pragma once

#include "D3D11Helpers.h"

class TiledDeferredRender
{
public:

	static constexpr uint32_t BUILD_FRUSTUM_BLOCK_X = 8;
	static constexpr uint32_t BUILD_FRUSTUM_BLOCK_Y = 8;

    struct Plane
    {
        Math::Vec3f n;   // normal (points inward)
        float  d;   // plane constant
    };

    struct TileFrustum
    {
        Plane left;
        Plane right;
        Plane top;
        Plane bottom;
    };

    struct TileCB
    {
        uint32_t gScreenWidth;
        uint32_t gScreenHeight;
        uint32_t gTilesX;
        uint32_t gTilesY;
	};

    void Create(ID3D11Device* dev,
        uint32_t screenWidth,
        uint32_t screenHeight,
        uint32_t tileSize,
        const wchar_t* csBuildFrustum);

	void BuildTileFrustums(ID3D11DeviceContext* ctx, ID3D11Buffer* camCB);

private:

	uint32_t m_screenWidth = 0;
	uint32_t m_screenHeight = 0;
	uint32_t m_tilesX = 0;
	uint32_t m_tilesY = 0;

	StructuredBufferSRVUAV m_tileFrustums;
	ComPtr<ID3D11Buffer> m_tileCB;

	ComPtr<ID3D11ComputeShader> m_csBuildFrustums;
};
