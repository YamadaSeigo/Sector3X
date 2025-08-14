#pragma once

#include "PhysicsTypes.h"

#include <../third_party/Jolt/Jolt.h>
#include <../third_party/Jolt/Physics/Collision/Shape/Shape.h>

namespace SectorFW
{
    namespace Physics
    {
        // IShapeResolver.h
        struct IShapeResolver {
            virtual ~IShapeResolver() = default;
            virtual JPH::RefConst<JPH::Shape> Resolve(ShapeHandle h) const = 0;
        };
    }
}
