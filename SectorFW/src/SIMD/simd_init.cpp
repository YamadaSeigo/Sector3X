// simd_init.cpp  （通常 /arch）
#include "SIMD/simd_api.h"
#include "SIMD/simd_detect.h"
#include <xmmintrin.h>

namespace SectorFW::SIMD
{
	// ===== ベース実装（宣言のみ。定義は simd_base.cpp） =====
	void UpdateScalarLerp_Base(float* dst, const float* a, const float* b,
		const uint32_t* mask01, size_t n, float alpha);
	void UpdateQuatNlerpShortest_Base(float* qx, float* qy, float* qz, float* qw,
		const float* ax, const float* ay, const float* az, const float* aw,
		const float* bx, const float* by, const float* bz, const float* bw,
		const uint32_t* mask01, size_t n, float alpha);

	// ===== AVX2 実装（宣言のみ。定義は simd_avx2.cpp） =====
	void UpdateScalarLerp_AVX2(float* dst, const float* a, const float* b,
		const uint32_t* mask01, size_t n, float alpha);
	void UpdateQuatNlerpShortest_AVX2(float* qx, float* qy, float* qz, float* qw,
		const float* ax, const float* ay, const float* az, const float* aw,
		const float* bx, const float* by, const float* bz, const float* bw,
		const uint32_t* mask01, size_t n, float alpha);

	// グローバル関数ポインタ定義
	UpdateScalarLerpFn        gUpdateScalarLerp = UpdateScalarLerp_Base;
	UpdateQuatNlerpShortestFn gUpdateQuatNlerpShortest = UpdateQuatNlerpShortest_Base;

	void SimdInit() {
		// 非正規数でのスローダウン回避（任意）
		_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
		_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

		if (cpu_has_avx2()) {
			gUpdateScalarLerp = UpdateScalarLerp_AVX2;
			gUpdateQuatNlerpShortest = UpdateQuatNlerpShortest_AVX2;
		}
		else {
			gUpdateScalarLerp = UpdateScalarLerp_Base;
			gUpdateQuatNlerpShortest = UpdateQuatNlerpShortest_Base;
		}
	}

	// 自動初期化したい場合（明示呼び出しでもOK）
	struct SimdAutoInit { SimdAutoInit() { SimdInit(); } } g_simd_auto_init;
}