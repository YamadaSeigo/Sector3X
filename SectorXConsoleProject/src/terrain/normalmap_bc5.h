#pragma once

#include <SectorFW/Math/Vector.hpp>

namespace TerrainUtil
{
    std::vector<uint8_t> EncodeNormalMapBC5(
        const SFW::Math::Vec3f* normals,
        int width,
        int height
    );
}
