// Morton2D.h
#pragma once
#include <cstdint>

namespace SectorFW {
	// --- オプション：負のセル座標を扱う場合の ZigZag 変換 ---
	constexpr inline uint64_t ZigZag64(int64_t v) noexcept {
		// [-2^63, 2^63-1] -> [0, 2^64-1]
		return (uint64_t(v) << 1) ^ uint64_t(v >> 63);
	}

	// ===== 2D Morton: (x,y) の下位32bitを 64bit にインターリーブ =====
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64))
  // CPU に BMI2 があれば PDEP で高速化
#include <immintrin.h>
	constexpr inline uint64_t Morton2D64(uint64_t x, uint64_t y) noexcept {
		// x,y の下位32bitのみ使用
		const uint64_t XMASK = 0x5555555555555555ull; // 偶数ビット
		const uint64_t YMASK = 0xAAAAAAAAAAAAAAAAull; // 奇数ビット
		return _pdep_u64(x & 0xFFFFFFFFull, XMASK) |
			_pdep_u64(y & 0xFFFFFFFFull, YMASK);
	}
#else
  // フォールバック：ビット展開（part1by1）
	constexpr inline uint64_t Part1By1(uint64_t v) noexcept {
		v &= 0x00000000FFFFFFFFull;
		v = (v | (v << 16)) & 0x0000FFFF0000FFFFull;
		v = (v | (v << 8)) & 0x00FF00FF00FF00FFull;
		v = (v | (v << 4)) & 0x0F0F0F0F0F0F0F0Full;
		v = (v | (v << 2)) & 0x3333333333333333ull;
		v = (v | (v << 1)) & 0x5555555555555555ull;
		return v;
	}
	constexpr inline uint64_t Morton2D64(uint64_t x, uint64_t y) noexcept {
		return (Part1By1(y) << 1) | Part1By1(x);
	}
#endif

	// ===== 逆変換（任意）：64bit Morton -> 32bit x/y =====
	constexpr inline uint64_t Compact1By1(uint64_t v) noexcept {
		v &= 0x5555555555555555ull;
		v = (v ^ (v >> 1)) & 0x3333333333333333ull;
		v = (v ^ (v >> 2)) & 0x0F0F0F0F0F0F0F0Full;
		v = (v ^ (v >> 4)) & 0x00FF00FF00FF00FFull;
		v = (v ^ (v >> 8)) & 0x0000FFFF0000FFFFull;
		v = (v ^ (v >> 16)) & 0x00000000FFFFFFFFull;
		return v;
	}
	constexpr inline uint32_t Morton2D_DecodeX(uint64_t code) noexcept {
		return uint32_t(Compact1By1(code));
	}
	constexpr inline uint32_t Morton2D_DecodeY(uint64_t code) noexcept {
		return uint32_t(Compact1By1(code >> 1));
	}
} // namespace SectorFW
