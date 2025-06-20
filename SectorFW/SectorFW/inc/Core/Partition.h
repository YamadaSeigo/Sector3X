#pragma once

#include <vector>

#include "Math/Vector.hpp"
#include "Math/AABB.hpp"

#include "SpatialChunk.h"

namespace SectorFW
{
    enum class EOutOfBoundsPolicy {
        Reject,
        ClampToEdge
    };

    template <typename Derived>
    concept PartitionConcept = requires(Derived t, Math::Vector3 v, ChunkSizeType size, EOutOfBoundsPolicy policy) {
        Derived{ size,size,size };
        { t.GetChunk(v, policy) } -> std::same_as< std::optional<SpatialChunk*>>;
    };
}
