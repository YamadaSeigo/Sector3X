#pragma once

#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>


namespace SectorFW
{
	namespace Physics
	{
		struct PhysicsInterpolation {
			Math::Vec3f prevPos{}, currPos{};
			Math::Quatf prevRot{ 0,0,0,1 }, currRot{ 0,0,0,1 };
			uint32_t lastUpdatedFrame = 0; // 同期漏れ判定に使用
		};

		struct BodyComponent {
			JPH::BodyID body;   // 生成後にセット（読み取り用）
			uint16_t    world;  // 所属ワールド
			bool        kinematic{ false };
		};
	}
}
