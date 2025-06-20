#pragma once

#include <algorithm>
#include "Partition.h"

namespace SectorFW
{
	class Grid2DPartition
	{
	public:
		Grid2DPartition(ChunkSizeType chunkWidth, ChunkSizeType chunkHeight, ChunkSizeType chunkSize) :
			grid(chunkWidth, chunkHeight), chunkSize(chunkSize) {}

		std::optional<SpatialChunk*> GetChunk(Math::Vector3 location, EOutOfBoundsPolicy policy = EOutOfBoundsPolicy::Reject) {
			// 位置に基づいてチャンクを取得するロジックを実装
			// ここではダミーの実装を返す
			ChunkSizeType x = static_cast<ChunkSizeType>(floor(location.x / chunkSize));
			ChunkSizeType y = static_cast<ChunkSizeType>(floor(location.y / chunkSize));

			if (policy == EOutOfBoundsPolicy::ClampToEdge) {
				x = std::clamp(x, ChunkSizeType(0), grid.width() - 1);
				y = std::clamp(y, ChunkSizeType(0), grid.height() - 1);
				return &grid(x, y);
			}

			if (x < 0 || x >= grid.width() || y < 0 || y >= grid.height())
				return nullptr;

			return &grid(x, y);
		}

	private:
		Grid2D<SpatialChunk, ChunkSizeType> grid;
		ChunkSizeType chunkSize;
	};
}
