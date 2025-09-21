/*****************************************************************//**
 * @file   simd_api.h
 * @brief SIMD最適化された関数のAPI定義ヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <cstddef>
#include <cstdint>

namespace SectorFW
{
	namespace SIMD
	{
		/**
		 * @brief スカラー補間を行う関数ポインタの型定義
		 */
		using UpdateScalarLerpFn = void(*)(float* dst, const float* a, const float* b,
			const uint32_t* mask01, size_t n, float alpha);
		/**
		 * @brief クォータニオンの最短経路補間を行う関数ポインタの型定義
		 */
		using UpdateQuatNlerpShortestFn = void(*)(float* qx, float* qy, float* qz, float* qw,
			const float* ax, const float* ay, const float* az, const float* aw,
			const float* bx, const float* by, const float* bz, const float* bw,
			const uint32_t* mask01, size_t n, float alpha);

		/**
		 * @brief 実行時に差し替えるスカラー補間関数ポインタ
		 */
		extern UpdateScalarLerpFn            gUpdateScalarLerp;
		/**
		 * @brief 実行時に差し替えるクォータニオン補間関数ポインタ
		 */
		extern UpdateQuatNlerpShortestFn     gUpdateQuatNlerpShortest;
		/**
		 * @brief SIMD初期化。起動時に呼ぶ
		 */
		void SimdInit();
	}
}
