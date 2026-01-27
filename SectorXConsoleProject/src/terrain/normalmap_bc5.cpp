#include "normalmap_bc5.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <SectorFW/Math/Vector.hpp>

namespace TerrainUtil
{
    static inline uint8_t NormalToUNorm8(float v)
    {
        float u = v * 0.5f + 0.5f;
        u = std::clamp(u, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::round(u * 255.0f));
    }

    static inline void EncodeBC4Block(const uint8_t src[16], uint8_t dst[8])
    {
        uint8_t vMin = 255, vMax = 0;
        for (int i = 0; i < 16; ++i) { vMin = (std::min)(vMin, src[i]); vMax = (std::max)(vMax, src[i]); }
        if (vMin == vMax) { dst[0] = vMax; dst[1] = vMin; for (int i = 0; i < 6; ++i) dst[i + 2] = 0; return; }

        uint8_t ep0 = vMax, ep1 = vMin;
        if (ep0 == ep1) { if (ep0 < 255) ++ep0; else --ep1; }
        dst[0] = ep0; dst[1] = ep1;

        float palette[8];
        palette[0] = ep0 / 255.0f;
        palette[1] = ep1 / 255.0f;
        for (int i = 1; i <= 6; ++i) {
            float v = ((7 - i) * ep0 + i * ep1) / 7.0f;
            palette[i + 1] = v / 255.0f;
        }

        uint8_t indices[16];
        for (int i = 0; i < 16; ++i) {
            float val = src[i] / 255.0f;
            float bestErr = 1e9f;
            uint8_t bestIdx = 0;
            for (uint8_t j = 0; j < 8; ++j) {
                float d = val - palette[j];
                float err = d * d;
                if (err < bestErr) { bestErr = err; bestIdx = j; }
            }
            indices[i] = bestIdx;
        }

        uint64_t bits = 0;
        for (int i = 0; i < 16; ++i) bits |= (uint64_t(indices[i] & 7u) << (3 * i));
        for (int i = 0; i < 6; ++i) dst[2 + i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu);
    }

    std::vector<uint8_t> EncodeNormalMapBC5(const SFW::Math::Vec3f* normals, int width, int height)
    {
        assert(normals && width > 0 && height > 0);
        assert((width % 4) == 0 && (height % 4) == 0);

        const int blockCountX = width / 4;
        const int blockCountY = height / 4;
        const int totalBlocks = blockCountX * blockCountY;

        std::vector<uint8_t> out(size_t(totalBlocks) * 16);

        uint8_t blockR[16], blockG[16];
        size_t dstOffset = 0;

        for (int by = 0; by < blockCountY; ++by) {
            for (int bx = 0; bx < blockCountX; ++bx) {
                for (int iy = 0; iy < 4; ++iy) {
                    for (int ix = 0; ix < 4; ++ix) {
                        int x = bx * 4 + ix;
                        int y = by * 4 + iy;
                        int srcIdx = y * width + x;
                        const auto& n = normals[srcIdx];

                        int texelIndex = iy * 4 + ix;
                        blockR[texelIndex] = NormalToUNorm8(n.x);
                        blockG[texelIndex] = NormalToUNorm8(n.z);
                    }
                }

                uint8_t* dstBlock = out.data() + dstOffset;
                EncodeBC4Block(blockR, dstBlock + 0);
                EncodeBC4Block(blockG, dstBlock + 8);
                dstOffset += 16;
            }
        }
        return out;
    }
}
