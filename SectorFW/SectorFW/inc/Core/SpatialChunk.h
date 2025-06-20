#pragma once

#include "../Math/AABB.hpp"
#include "ECS/EntityManager.h"

namespace SectorFW
{
	class SpatialChunk
	{
	public:
		typedef uint32_t SizeType;

		SpatialChunk() :entityManager(std::make_unique<ECS::EntityManager>()) {}

		ECS::EntityManager& GetEntityManager() const { return *entityManager; }

	private:
		Math::AABB3 aabb;
		std::unique_ptr<ECS::EntityManager> entityManager;
	};

	using ChunkSizeType = SpatialChunk::SizeType;
}
