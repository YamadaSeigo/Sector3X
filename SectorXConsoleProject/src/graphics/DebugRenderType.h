#pragma once


struct DebugRenderType
{
	DebugRenderType();

	static bool isHit;
	static bool drawPartitionBounds;
	static bool drawFrustumBounds;
	static bool drawModelAABB;
	static bool drawOccluderAABB;
	static bool drawModelRect;
	static bool drawOcclusionRect;
	static bool drawCascadeAABB;
	static bool drawShapeDims;
	static bool drawMOCDepth;
	static bool drawFireflyVolumes;
	static bool drawLeafVolumes;
	static bool drawBloom;

	inline static constexpr const char* ShowDeferredBufferName[] =
	{
		"albedo",
		"normal",
		"emissive",
		"ao",
		"roughness",
		"metallic"
	};

	static bool drawDeferredTextureFlags[sizeof(ShowDeferredBufferName) / sizeof(ShowDeferredBufferName[0])];

	static bool drawTileLight;

	static Graphics::TextureHandle debugBloomTexHandle;
};

//コンストラクタを呼び出すため
extern const DebugRenderType debugRenderType;
