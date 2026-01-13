
#include "DebugRenderType.h"
#include "DeferredRenderingService.h"

#include <SectorFW/Debug/UIBus.h>

bool DebugRenderType::isHit = false;
bool DebugRenderType::drawPartitionBounds = false;
bool DebugRenderType::drawFrustumBounds = false;
bool DebugRenderType::drawModelAABB = false;
bool DebugRenderType::drawOccluderAABB = false;
bool DebugRenderType::drawModelRect = false;
bool DebugRenderType::drawOcclusionRect = false;
bool DebugRenderType::drawCascadeAABB = false;
bool DebugRenderType::drawShapeDims = false;
bool DebugRenderType::drawMOCDepth = false;
bool DebugRenderType::drawFireflyVolumes = false;
bool DebugRenderType::drawBloom = false;

Graphics::TextureHandle DebugRenderType::debugBloomTexHandle = {};

bool DebugRenderType::drawDeferredTextureFlags[sizeof(ShowDeferredBufferName) / sizeof(ShowDeferredBufferName[0])] = { false };

const DebugRenderType debugRenderType = {};

DebugRenderType::DebugRenderType()
{
	//ã≠êßìIÇ…UI BusÇäJénÇµÇƒÇ®Ç≠
	Debug::StartUIBus();

	BIND_DEBUG_CHECKBOX("Show", "enabled", &isHit);
	BIND_DEBUG_CHECKBOX("Show", "partition", &drawPartitionBounds);
	BIND_DEBUG_CHECKBOX("Show", "frustum", &drawFrustumBounds);
	BIND_DEBUG_CHECKBOX("Show", "modelAABB", &drawModelAABB);
	BIND_DEBUG_CHECKBOX("Show", "occAABB", &drawOccluderAABB);
	BIND_DEBUG_CHECKBOX("Show", "modelRect", &drawModelRect);
	BIND_DEBUG_CHECKBOX("Show", "occlusionRect", &drawOcclusionRect);
	BIND_DEBUG_CHECKBOX("Show", "cascadesAABB", &drawCascadeAABB);
	BIND_DEBUG_CHECKBOX("Show", "shapeDims", &drawShapeDims);
	BIND_DEBUG_CHECKBOX("Show", "fireflyVolumes", &drawFireflyVolumes);

	constexpr auto drawDeferredBufferCount = sizeof(ShowDeferredBufferName) / sizeof(ShowDeferredBufferName[0]);
	assert(drawDeferredBufferCount == DeferredTextureCount * 2);
	for (size_t i = 0; i < drawDeferredBufferCount; ++i)
	{
		BIND_DEBUG_CHECKBOX("Screen", ShowDeferredBufferName[i], &drawDeferredTextureFlags[i]);
	}

	BIND_DEBUG_CHECKBOX("Screen", "moc", &drawMOCDepth);
	BIND_DEBUG_CHECKBOX("Screen", "bloom", &drawBloom);
}
