#pragma once

#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

namespace SectorFW
{
	struct Transform
	{
		Math::Vector3 location;
		Math::Quaternion rotation;
		Math::Vector3 scale;
	};
}
