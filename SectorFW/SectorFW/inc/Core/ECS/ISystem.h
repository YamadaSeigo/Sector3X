#pragma once

#include "Accessor.h"
#include "Util/CommonTypes.h"

namespace SectorFW
{
	namespace ECS
	{
		class EntityManager;
		class SpatialChunk;

		//----------------------------------------------
	   // System Interface
	   //----------------------------------------------
		template<typename Partition>
		class ISystem {
		public:
			virtual void Update(Partition& partition) = 0;
			virtual AccessInfo GetAccessInfo() const noexcept = 0;
			virtual ~ISystem() = default;
		};
	}
}
