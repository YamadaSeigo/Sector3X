#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "PhysicsTypes.h"        // ShapeHandle, ShapeCreateDesc, ConvexHullDesc, ShapeScale

namespace SFW::Physics
{
    // -------------------------------
    // ローカル用のデータ構造
    // -------------------------------

    // .chullbin を読み込んで全部のハルを outHulls に積む
    // 戻り値: 成功なら true / 失敗なら false
    inline bool LoadVHACDFile(
        const std::filesystem::path& binPath,
        std::vector<VHACDHull>& outHulls,
		const Math::Vec3f scale = { 1,1,1 },
        bool flip = false)
    {
        outHulls.clear();

        std::ifstream ifs(binPath, std::ios::binary);
        if (!ifs)
        {
			LOG_ERROR("PhysicsConvexHullLoader: Failed to open VHACD file: {%s}", binPath.string().c_str());

            // ファイルが開けない
            return false;
        }

        // ----- ヘッダー読込 -----
        char magic[4];
        ifs.read(magic, 4);
        if (!ifs || magic[0] != 'C' || magic[1] != 'V' || magic[2] != 'X' || magic[3] != 'H')
        {
			LOG_ERROR("PhysicsConvexHullLoader: Invalid VHACD file format: {%s}", binPath.string().c_str());

            // フォーマット不一致
            return false;
        }

        auto read_u32 = [&ifs]() -> uint32_t
            {
                uint32_t v{};
                ifs.read(reinterpret_cast<char*>(&v), sizeof(v));
                return v;
            };

        uint32_t version = read_u32();
        uint32_t hullCount = read_u32();
        if (!ifs) return false;
        if (version != 1)
        {
			LOG_ERROR("PhysicsConvexHullLoader: Unsupported VHACD file version (%u) in file: {%s}", version, binPath.string().c_str());

            // とりあえず version 1 のみ対応
            return false;
        }

        outHulls.reserve(hullCount);

        // ----- 各ハル -----
        for (uint32_t h = 0; h < hullCount; ++h)
        {
            uint32_t vCount = read_u32();
            uint32_t iCount = read_u32();
            if (!ifs) return false;

            VHACDHull hull;
            hull.points.resize(vCount);
            hull.indices.resize(iCount);

            auto flip_vec3z = [](float& x, float& y, float& z) { x = -x; };

            // 頂点 (vCount * 3 float)
            for (uint32_t i = 0; i < vCount; ++i)
            {
                float xyz[3];
                ifs.read(reinterpret_cast<char*>(xyz), sizeof(float) * 3);
                if (!ifs) return false;

				if(flip) flip_vec3z(xyz[0], xyz[1], xyz[2]);
                hull.points[i] = Vec3f{ xyz[0], xyz[1], xyz[2] } * scale;
            }

            // インデックス (iCount * uint32)
            if (iCount > 0)
            {
                ifs.read(reinterpret_cast<char*>(hull.indices.data()),
                    sizeof(uint32_t) * iCount);
                if (!ifs) return false;
            }

            outHulls.emplace_back(std::move(hull));
        }

        return true;
    }  

} // namespace SFW::Physics
