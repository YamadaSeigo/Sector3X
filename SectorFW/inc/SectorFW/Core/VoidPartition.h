#pragma once

#include "partition.hpp"

namespace SFW
{
	/**
	 * @brief 空間分割をしないパーティションクラス
	 */
	class VoidPartition
	{
	public:
		VoidPartition(ChunkSizeType, ChunkSizeType, float) {}

		std::optional<SpatialChunk*> GetChunk(Math::Vec3f,
			SpatialChunkRegistry&, LevelID,
			EOutOfBoundsPolicy = EOutOfBoundsPolicy::ClampToEdge) noexcept
		{
			return &m_chunk;
		}

		ECS::EntityManager& GetGlobalEntityManager() noexcept { return m_chunk.GetEntityManager(); }

		void RegisterAllChunks(SpatialChunkRegistry&, LevelID) {}

		size_t GetEntityNum() const noexcept
		{
			return m_chunk.GetEntityManager().GetEntityCount();
		}

		std::vector<SpatialChunk*> CullChunks(const Math::Frustumf&) noexcept
		{
			return { &m_chunk };
		}

		std::vector<SpatialChunk*> CullChunksNear(const Math::Frustumf&, Math::Vec3f) noexcept
		{
			return { &m_chunk };
		}

		uint32_t CullChunkLine(const Math::Frustumf&,
			Math::Vec3f, float,
			Debug::LineVertex*, uint32_t, uint32_t) const noexcept
		{
			return 0;
		}

		void CleanChunk()
		{
			m_chunk.GetEntityManager().CleanAllEntity();
		}

	private:
		SpatialChunk m_chunk;
	};
}
