// PhysicsSnapshot.h
#pragma once
#include <vector>
#include <cstdint>

#include "Core/ECS/entity.h"
#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

namespace SectorFW
{
	namespace Physics
	{
		struct Pose {
			ECS::EntityID e;
			Math::Vec3f  pos;
			Math::Quatf  rot;
		};

		struct ContactEvent {
			enum Type { Begin, Persist, End } type;
			ECS::EntityID a, b;
			Math::Vec3f  pointWorld;
			Math::Vec3f  normalWorld;
			float  impulse;
		};

		struct RayCastHitEvent {
			uint32_t requestId;
			bool     hit;
			ECS::EntityID   hitEntity;
			Math::Vec3f    position;
			Math::Vec3f    normal;
			float    distance;
		};

		struct PhysicsSnapshot {
			std::vector<Pose>          poses;       // curr (ëOÉtÉåÅ[ÉÄÇÕï Ç…ï€éùÇµÇƒï‚ä‘)
			std::vector<ContactEvent>  contacts;
			std::vector<RayCastHitEvent> rayHits;
		};
	}
}
