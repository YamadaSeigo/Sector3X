#pragma once
#include <cstdint>
#include <string>

namespace Assets
{
	enum : uint32_t {
		Mat_Grass = 1, Mat_Rock = 2, Mat_Dirt = 3, Mat_Snow = 4,
		Mat_Normal_Grass = 5, Mat_Normal_Rock = 6, Mat_Normal_Dirt = 7, Mat_Normal_Snow = 8,
		Tex_Splat_Control_0 = 10001,
		Tex_Biome_Control_0 = 20001,
	};

	bool ResolveTexturePath(uint32_t id, std::string& path, bool& forceSRGB);
}
