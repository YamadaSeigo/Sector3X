#pragma once
#include <cstdint>
#include <string>

namespace Assets
{
    enum : uint32_t {
        Mat_Grass = 1, Mat_Rock = 2, Mat_Dirt = 3, Mat_Snow = 4,
        Tex_Splat_Control_0 = 10001,
    };

    bool ResolveTexturePath(uint32_t id, std::string& path, bool& forceSRGB);
}
