// simd_base.cpp
#include "SIMD/simd_api.h"
#include <cmath>
#include <algorithm>

namespace SectorFW::SIMD
{
	void UpdateScalarLerp_Base(float* dst, const float* a, const float* b,
		const uint32_t* mask01, size_t n, float alpha)
	{
		for (size_t i = 0; i < n; ++i) {
			if (!mask01 || mask01[i]) {
				dst[i] = a[i] + (b[i] - a[i]) * alpha;
			}
		}
	}

	// Å’ZŒo˜H nlerpF dot(a,b)<0 ‚È‚ç b ‚ð”½“] ¨ lerp ¨ ³‹K‰»
	void UpdateQuatNlerpShortest_Base(float* qx, float* qy, float* qz, float* qw,
		const float* ax, const float* ay, const float* az, const float* aw,
		const float* bx, const float* by, const float* bz, const float* bw,
		const uint32_t* mask01, size_t n, float alpha)
	{
		for (size_t i = 0; i < n; ++i) {
			if (mask01 && !mask01[i]) continue;

			float bxi = bx[i], byi = by[i], bzi = bz[i], bwi = bw[i];
			const float dot = ax[i] * bxi + ay[i] * byi + az[i] * bzi + aw[i] * bwi;
			if (dot < 0.f) { bxi = -bxi; byi = -byi; bzi = -bzi; bwi = -bwi; }

			float x = ax[i] + (bxi - ax[i]) * alpha;
			float y = ay[i] + (byi - ay[i]) * alpha;
			float z = az[i] + (bzi - az[i]) * alpha;
			float w = aw[i] + (bwi - aw[i]) * alpha;

			const float len2 = x * x + y * y + z * z + w * w;
			const float invL = 1.0f / std::sqrt(std::max(len2, 1e-30f));
			qx[i] = x * invL; qy[i] = y * invL; qz[i] = z * invL; qw[i] = w * invL;
		}
	}
}