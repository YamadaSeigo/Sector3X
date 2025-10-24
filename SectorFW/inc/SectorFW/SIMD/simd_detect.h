/*****************************************************************//**
 * @file   simd_detect.h
 * @brief SIMD命令セットのサポートを検出するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <intrin.h>
#include <cstdint>

namespace SFW
{
	namespace SIMD
	{
		/**
		 * @brief CPUがAVXをサポートしているかをチェックする関数
		 * @return bool AVXサポートの有無
		 */
		inline bool cpu_has_avx_os_support() {
			int info[4]{};
			__cpuid(info, 1);
			const bool osxsave = (info[2] & (1 << 27)) != 0; // ECX.OSXSAVE
			const bool avx = (info[2] & (1 << 28)) != 0; // ECX.AVX
			if (!(osxsave && avx)) return false;

			// XCR0: XMM(bit1) と YMM(bit2) が OS により有効か
			unsigned long long xcr0 = _xgetbv(0);
			const bool xmm_ok = (xcr0 & 0x2) != 0;
			const bool ymm_ok = (xcr0 & 0x4) != 0;
			return xmm_ok && ymm_ok;
		}
		/**
		 * @brief CPUがAVX2をサポートしているかをチェックする関数
		 * @return bool AVX2サポートの有無
		 */
		inline bool cpu_has_avx2() {
			if (!cpu_has_avx_os_support()) return false;
			int info7[4]{};
			__cpuidex(info7, 7, 0);
			const bool avx2 = (info7[1] & (1 << 5)) != 0; // EBX.AVX2
			return avx2;
		}
	}
}
