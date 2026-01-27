#pragma once
#include <cstdint>

#define WINDOW_NAME "SectorX Console Project"

namespace App
{
    constexpr uint32_t WINDOW_WIDTH = uint32_t(1920 / 1.5f);
    constexpr uint32_t WINDOW_HEIGHT = uint32_t(1080 / 1.5f);

    constexpr uint32_t SHADOW_MAP_SIZE = 1024 / 2;
    constexpr double   FPS_LIMIT = 60.0;
}
