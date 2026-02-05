#pragma once

#include "D3D11Helpers.h"

class TiledDeferredRender
{
public:

	static constexpr uint32_t TILE_SIZE = 16;

    static constexpr uint32_t MAX_LIGHTS_PER_TILE = 128;

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
        const wchar_t* csBuildFrustum,
        const wchar_t* csTileCulling,
        const wchar_t* csDrawTileLight);

	void BuildTileFrustums(ID3D11DeviceContext* ctx, ID3D11Buffer* camCB);
    void TileCullingLight(ID3D11DeviceContext* ctx, 
        ID3D11ShaderResourceView* normalLightSRV,
        ID3D11ShaderResourceView* fireflyLightSRV,
        ID3D11ShaderResourceView* depthSRV,
        ID3D11Buffer* camCB, 
        ID3D11Buffer* lightCountCB);

    void DrawTileLight(ID3D11DeviceContext* ctx,
        ID3D11ShaderResourceView* normalLightSRV,
        ID3D11ShaderResourceView* fireflyLightSRV,
        ID3D11ShaderResourceView* albedoSRV,
        ID3D11ShaderResourceView* normalSRV,
        ID3D11ShaderResourceView* depthSRV,
        ID3D11UnorderedAccessView* outLightTex,
        ID3D11SamplerState* pointSampler,
        ID3D11Buffer* camCB);

private:

	uint32_t m_screenWidth = 0;
	uint32_t m_screenHeight = 0;
	uint32_t m_tilesX = 0;
	uint32_t m_tilesY = 0;

	StructuredBufferSRVUAV m_tileFrustums;
	StructuredBufferSRVUAV m_tileLightIndices;
	StructuredBufferSRVUAV m_lightIndexCounter;

	ComPtr<ID3D11Buffer> m_tileCB;

	ComPtr<ID3D11ComputeShader> m_csBuildFrustums;
    ComPtr<ID3D11ComputeShader> m_csTileCulling;
    ComPtr<ID3D11ComputeShader> m_csDrawTileLight;
};
