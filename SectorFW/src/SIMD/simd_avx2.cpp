// simd_avx2.cpp
#include "SIMD/simd_api.h"
#include <immintrin.h>

namespace SectorFW::SIMD
{
	void UpdateScalarLerp_AVX2(float* dst, const float* a, const float* b,
		const uint32_t* mask01, size_t n, float alpha)
	{
#if defined(__AVX2__)
		const __m256 vA = _mm256_set1_ps(alpha);
		const __m256i zI = _mm256_setzero_si256();
		size_t i = 0;
		for (; i + 8 <= n; i += 8) {
			__m256 va = _mm256_loadu_ps(a + i);
			__m256 vb = _mm256_loadu_ps(b + i);
			__m256 vlerp = _mm256_fmadd_ps(_mm256_sub_ps(vb, va), vA, va); // a + (b-a)*alpha

			if (!mask01) {
				_mm256_storeu_ps(dst + i, vlerp);
				continue;
			}

			__m256 old = _mm256_loadu_ps(dst + i);
			__m256  m = _mm256_castsi256_ps(
				_mm256_cmpgt_epi32(
					_mm256_loadu_si256((const __m256i*)(mask01 + i)), zI));
			// タイル最適化：全0/全1 なら分岐
			int mm = _mm256_movemask_ps(m);
			if (mm == 0xFF) { _mm256_storeu_ps(dst + i, vlerp); }
			else if (mm == 0x00) { /* no-op */ }
			else {
				_mm256_storeu_ps(dst + i, _mm256_blendv_ps(old, vlerp, m));
			}
		}
		for (; i < n; ++i) {
			const float v = a[i] + (b[i] - a[i]) * alpha;
			if (!mask01 || mask01[i]) dst[i] = v;
		}
#else
		(void)dst; (void)a; (void)b; (void)mask01; (void)n; (void)alpha;
#endif
	}

	void UpdateQuatNlerpShortest_AVX2(float* qx, float* qy, float* qz, float* qw,
		const float* ax, const float* ay, const float* az, const float* aw,
		const float* bx, const float* by, const float* bz, const float* bw,
		const uint32_t* mask01, size_t n, float alpha)
	{
#if defined(__AVX2__)
		const __m256 vA = _mm256_set1_ps(alpha);
		const __m256 sign = _mm256_set1_ps(-0.0f); // 符号ビット
		const __m256i zI = _mm256_setzero_si256();

		size_t i = 0;
		for (; i + 8 <= n; i += 8) {
			__m256 axv = _mm256_loadu_ps(ax + i), ayv = _mm256_loadu_ps(ay + i), azv = _mm256_loadu_ps(az + i), awv = _mm256_loadu_ps(aw + i);
			__m256 bxv = _mm256_loadu_ps(bx + i), byv = _mm256_loadu_ps(by + i), bzv = _mm256_loadu_ps(bz + i), bwv = _mm256_loadu_ps(bw + i);

			// dot(a,b)
			__m256 dot = _mm256_fmadd_ps(axv, bxv, _mm256_fmadd_ps(ayv, byv, _mm256_fmadd_ps(azv, bzv, _mm256_mul_ps(awv, bwv))));
			// dot<0 → b を反転 (XOR 符号)
			__m256 negMask = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_setzero_si256(), _mm256_castps_si256(dot)));
			bxv = _mm256_xor_ps(bxv, _mm256_and_ps(sign, negMask));
			byv = _mm256_xor_ps(byv, _mm256_and_ps(sign, negMask));
			bzv = _mm256_xor_ps(bzv, _mm256_and_ps(sign, negMask));
			bwv = _mm256_xor_ps(bwv, _mm256_and_ps(sign, negMask));

			// lerp
			__m256 lx = _mm256_fmadd_ps(_mm256_sub_ps(bxv, axv), vA, axv);
			__m256 ly = _mm256_fmadd_ps(_mm256_sub_ps(byv, ayv), vA, ayv);
			__m256 lz = _mm256_fmadd_ps(_mm256_sub_ps(bzv, azv), vA, azv);
			__m256 lw = _mm256_fmadd_ps(_mm256_sub_ps(bwv, awv), vA, awv);

			// normalize (nlerp)
			__m256 len2 = _mm256_fmadd_ps(lx, lx, _mm256_fmadd_ps(ly, ly, _mm256_fmadd_ps(lz, lz, _mm256_mul_ps(lw, lw))));
			__m256 invL = _mm256_rsqrt_ps(len2);
			lx = _mm256_mul_ps(lx, invL); ly = _mm256_mul_ps(ly, invL); lz = _mm256_mul_ps(lz, invL); lw = _mm256_mul_ps(lw, invL);

			if (!mask01) {
				_mm256_storeu_ps(qx + i, lx); _mm256_storeu_ps(qy + i, ly); _mm256_storeu_ps(qz + i, lz); _mm256_storeu_ps(qw + i, lw);
				continue;
			}
			__m256 m = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_loadu_si256((const __m256i*)(mask01 + i)), zI));
			int mm = _mm256_movemask_ps(m);
			if (mm == 0xFF) {
				_mm256_storeu_ps(qx + i, lx); _mm256_storeu_ps(qy + i, ly); _mm256_storeu_ps(qz + i, lz); _mm256_storeu_ps(qw + i, lw);
			}
			else if (mm == 0x00) {
				// no-op
			}
			else {
				__m256 ox = _mm256_loadu_ps(qx + i), oy = _mm256_loadu_ps(qy + i), oz = _mm256_loadu_ps(qz + i), ow = _mm256_loadu_ps(qw + i);
				_mm256_storeu_ps(qx + i, _mm256_blendv_ps(ox, lx, m));
				_mm256_storeu_ps(qy + i, _mm256_blendv_ps(oy, ly, m));
				_mm256_storeu_ps(qz + i, _mm256_blendv_ps(oz, lz, m));
				_mm256_storeu_ps(qw + i, _mm256_blendv_ps(ow, lw, m));
			}
		}
		for (; i < n; ++i) {
			if (mask01 && !mask01[i]) continue;
			float bxi = bx[i], byi = by[i], bzi = bz[i], bwi = bw[i];
			const float dot = ax[i] * bxi + ay[i] * byi + az[i] * bzi + aw[i] * bwi;
			if (dot < 0.f) { bxi = -bxi; byi = -byi; bzi = -bzi; bwi = -bwi; }
			float x = ax[i] + (bxi - ax[i]) * alpha;
			float y = ay[i] + (byi - ay[i]) * alpha;
			float z = az[i] + (bzi - az[i]) * alpha;
			float w = aw[i] + (bwi - aw[i]) * alpha;
			float invL = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
			qx[i] = x * invL; qy[i] = y * invL; qz[i] = z * invL; qw[i] = w * invL;
		}
#else
		(void)qx; (void)qy; (void)qz; (void)qw; (void)ax; (void)ay; (void)az; (void)aw;
		(void)bx; (void)by; (void)bz; (void)bw; (void)mask01; (void)n; (void)alpha;
#endif
	}
}