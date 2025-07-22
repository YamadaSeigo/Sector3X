#pragma once

#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

#include "Util/Flatten.hpp"

namespace SectorFW
{
	struct Transform
	{
		Math::Vec3f location;
		Math::Quatf rotation;
		Math::Vec3f scale;
	};
}
