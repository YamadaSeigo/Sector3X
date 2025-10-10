#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <cmath>
#include <limits>

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#include <immintrin.h>
#define SFW_HAS_AVX2 1
#else
#define SFW_HAS_AVX2 0
#endif

#include "ECS/ArchetypeChunk.h"


namespace SectorFW
{

    struct SoAPositions {
        const float* x; // [N]
        const float* y; // [N]
        const float* z; // [N]
        uint32_t     N;
    };

    template<int B = 32>
    inline int DistanceBin(float d2, float near2, float far2) noexcept {
        float t = (d2 - near2) / (std::max)(far2 - near2, 1e-12f);
        int b = (int)(t * B);
        return (b < 0 ? 0 : (b >= B ? B - 1 : b));
    }

    // dx^2 + dy^2 + dz^2（カメラ位置からの距離^2）
    inline float Dist2(const SoAPositions& T, uint32_t i,
        float cx, float cy, float cz) noexcept {
        float dx = T.x[i] - cx;
        float dy = T.y[i] - cy;
        float dz = T.z[i] - cz;
        return dx * dx + dy * dy + dz * dz;
    }

    // 近→遠の近似順で order を構築（安定）
    template<int B = 32>
    void BuildNearToFar_Order_Buckets(const SoAPositions& T,
        float cx, float cy, float cz,
        float near2, float far2,
        std::vector<uint32_t>& order)
    {
        const uint32_t N = T.N;
        order.resize(N);

        // 1) 各ビンの個数カウント
        uint32_t count[B] = {}; // 0 init
        std::vector<uint8_t> bins(N); // i番目がどのビンか

        for (uint32_t i = 0; i < N; ++i) {
            float d2 = Dist2(T, i, cx, cy, cz);
            int b = DistanceBin<B>(d2, near2, far2);
            bins[i] = (uint8_t)b;
            ++count[b];
        }

        // 2) prefix sum → 書き込み開始位置
        uint32_t offset[B];
        uint32_t sum = 0;
        for (int b = 0; b < B; ++b) {
            offset[b] = sum;
            sum += count[b];
        }

        // 3) 安定に連結
        for (uint32_t i = 0; i < N; ++i) {
            int b = bins[i];
            order[offset[b]++] = i;
        }
    }

    // d2 を [near2, far2] → [0, 65535] に量子化（固定小数キー）
    inline uint16_t QuantizeD2(float d2, float near2, float far2) noexcept {
        float t = (d2 - near2) / (std::max)(far2 - near2, 1e-12f);
        int v = (int)std::lrintf(t * 65535.0f);
        if (v < 0) v = 0; else if (v > 65535) v = 65535;
        return (uint16_t)v;
    }

    static void BuildOrder_FixedRadix16(const SoAPositions& T,
        float cx, float cy, float cz,
        float near2, float far2,
        std::vector<uint32_t>& order)
    {
        const uint32_t N = T.N;
        order.resize(N);
        std::vector<uint16_t> key(N);

#if SFW_HAS_AVX2
        const __m256 vxC = _mm256_set1_ps(cx);
        const __m256 vyC = _mm256_set1_ps(cy);
        const __m256 vzC = _mm256_set1_ps(cz);
        const __m256 vNear2 = _mm256_set1_ps(near2);
        const __m256 vScale = _mm256_set1_ps(65535.0f / (std::max)(far2 - near2, 1e-12f));

        uint32_t i = 0;
        for (; i + 8 <= N; i += 8) {
            __m256 vx = _mm256_loadu_ps(T.x + i);
            __m256 vy = _mm256_loadu_ps(T.y + i);
            __m256 vz = _mm256_loadu_ps(T.z + i);

            __m256 dx = _mm256_sub_ps(vx, vxC);
            __m256 dy = _mm256_sub_ps(vy, vyC);
            __m256 dz = _mm256_sub_ps(vz, vzC);

            __m256 d2 = _mm256_fmadd_ps(dx, dx,
                _mm256_fmadd_ps(dy, dy, _mm256_mul_ps(dz, dz)));

            // t = (d2 - near2) * scale
            __m256 t = _mm256_mul_ps(_mm256_sub_ps(d2, vNear2), vScale);

            // clamp [0,65535]
            __m256 zero = _mm256_set1_ps(0.0f);
            __m256 maxv = _mm256_set1_ps(65535.0f);
            t = _mm256_max_ps(zero, _mm256_min_ps(t, maxv));

            // 量子化（丸め）
            __m256i qi = _mm256_cvtps_epi32(t); // 32bit int
            alignas(32) uint32_t tmp[8];
            _mm256_store_si256((__m256i*)tmp, qi);
            for (int k = 0; k < 8; ++k) key[i + k] = (uint16_t)tmp[k];
        }
        for (; i < N; ++i) {
            key[i] = QuantizeD2(Dist2(T, i, cx, cy, cz), near2, far2);
        }
#else
        for (uint32_t i = 0; i < N; ++i) {
            key[i] = QuantizeD2(Dist2(T, i, cx, cy, cz), near2, far2);
        }
#endif

        // ---- 16bit key の2パス計数/Radix（安定） ----
        // LSB 8bit → MSB 8bit
        std::vector<uint32_t> tmpOrder(N);
        // 初期順（安定性維持のため）
        for (uint32_t i = 0; i < N; ++i) tmpOrder[i] = i;

        auto pass = [&](int shift, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst) {
            uint32_t cnt[256] = {};
            for (uint32_t i = 0; i < N; ++i) ++cnt[(key[src[i]] >> shift) & 0xFF];
            uint32_t ofs[256]; uint32_t sum = 0;
            for (int b = 0; b < 256; ++b) { ofs[b] = sum; sum += cnt[b]; }
            for (uint32_t i = 0; i < N; ++i) {
                uint16_t k = key[src[i]];
                uint32_t b = (k >> shift) & 0xFF;
                dst[ofs[b]++] = src[i];
            }
            };

        pass(0, tmpOrder, order);      // LSB
        pass(8, order, tmpOrder);      // MSB
        order.swap(tmpOrder);          // 最終結果を order に
    }

    struct KeyRow { float dist2; uint32_t row; };

    static void BuildFrontK_Strict(const SoAPositions& T,
        float cx, float cy, float cz,
        uint32_t K,
        std::vector<uint32_t>& order)
    {
        const uint32_t N = T.N;
        order.resize(N);
        std::vector<KeyRow> keys(N);
        for (uint32_t i = 0; i < N; ++i) {
            keys[i] = { Dist2(T, i, cx, cy, cz), i };
        }
        if (K > N) K = N;
        std::nth_element(keys.begin(), keys.begin() + K, keys.end(),
            [](auto& a, auto& b) { return a.dist2 < b.dist2; });
        std::sort(keys.begin(), keys.begin() + K,
            [](auto& a, auto& b) { return a.dist2 < b.dist2; });

        for (uint32_t i = 0; i < N; ++i) order[i] = keys[i].row;
    }
}
