// Morton2D.h
#pragma once
#include <cstdint>

namespace SectorFW {
	// --- オプション：負のセル座標を扱う場合の ZigZag 変換 ---
	[[nodiscard]] static inline constexpr uint64_t ZigZag64(int64_t v) noexcept {
		// [-2^63, 2^63-1] -> [0, 2^64-1]
		return (uint64_t(v) << 1) ^ uint64_t(v >> 63);
	}

	// ===== 2D Morton: (x,y) の下位32bitを 64bit にインターリーブ =====
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64))
  // CPU に BMI2 があれば PDEP で高速化
#include <immintrin.h>
	[[nodiscard]] static inline constexpr uint64_t Morton2D64(uint64_t x, uint64_t y) noexcept {
		// x,y の下位32bitのみ使用
		const uint64_t XMASK = 0x5555555555555555ull; // 偶数ビット
		const uint64_t YMASK = 0xAAAAAAAAAAAAAAAAull; // 奇数ビット
		return _pdep_u64(x & 0xFFFFFFFFull, XMASK) |
			_pdep_u64(y & 0xFFFFFFFFull, YMASK);
	}
#else
  // フォールバック：ビット展開（part1by1）
	[[nodiscard]] static inline constexpr uint64_t Part1By1(uint64_t v) noexcept {
		v &= 0x00000000FFFFFFFFull;
		v = (v | (v << 16)) & 0x0000FFFF0000FFFFull;
		v = (v | (v << 8)) & 0x00FF00FF00FF00FFull;
		v = (v | (v << 4)) & 0x0F0F0F0F0F0F0F0Full;
		v = (v | (v << 2)) & 0x3333333333333333ull;
		v = (v | (v << 1)) & 0x5555555555555555ull;
		return v;
	}
	[[nodiscard]] static inline constexpr uint64_t Morton2D64(uint64_t x, uint64_t y) noexcept {
		return (Part1By1(y) << 1) | Part1By1(x);
	}
#endif

	// ===== 逆変換（任意）：64bit Morton -> 32bit x/y =====
	[[nodiscard]] static inline constexpr uint64_t Compact1By1(uint64_t v) noexcept {
		v &= 0x5555555555555555ull;
		v = (v ^ (v >> 1)) & 0x3333333333333333ull;
		v = (v ^ (v >> 2)) & 0x0F0F0F0F0F0F0F0Full;
		v = (v ^ (v >> 4)) & 0x00FF00FF00FF00FFull;
		v = (v ^ (v >> 8)) & 0x0000FFFF0000FFFFull;
		v = (v ^ (v >> 16)) & 0x00000000FFFFFFFFull;
		return v;
	}
	[[nodiscard]] static inline constexpr uint32_t Morton2D_DecodeX(uint64_t code) noexcept {
		return uint32_t(Compact1By1(code));
	}
	[[nodiscard]] static inline constexpr uint32_t Morton2D_DecodeY(uint64_t code) noexcept {
		return uint32_t(Compact1By1(code >> 1));
	}

	[[nodiscard]] static inline constexpr std::int64_t  UnZigZag64(std::uint64_t u) noexcept {
		return static_cast<std::int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL)); // (u>>1) ^ -(u&1)
	}

	[[nodiscard]] static inline constexpr std::uint32_t ZigZag32(std::int32_t v) noexcept {
		return (static_cast<std::uint32_t>(v) << 1) ^ static_cast<std::uint32_t>(v >> 31);
	}
	[[nodiscard]] static inline constexpr std::int32_t  UnZigZag32(std::uint32_t u) noexcept {
		return static_cast<std::int32_t>((u >> 1) ^ (~(u & 1U) + 1U));
	}

	/* =============================================================
	 * Bit interleave helpers for 3D Morton 64-bit
	 * -------------------------------------------------------------
	 *  3D Morton は x,y,z の各ビットを [x0,y0,z0,x1,y1,z1,...] の順に並べる。
	 *  各軸 21bit まで安全 (21 * 3 = 63)。
	 * ============================================================= */
	[[nodiscard]] static inline constexpr std::uint64_t Part1By2_64(std::uint64_t x) noexcept {
		x &= 0x1fffffULL;                                 // 21 bits
		x = (x | (x << 32)) & 0x1f00000000ffffULL;
		x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
		x = (x | (x << 8)) & 0x100f00f00f00f00fULL;
		x = (x | (x << 4)) & 0x10c30c30c30c30c3ULL;
		x = (x | (x << 2)) & 0x1249249249249249ULL;   // 0b001001.. パターン
		return x;
	}

	[[nodiscard]] static inline constexpr std::uint64_t Compact1By2_64(std::uint64_t x) noexcept {
		x &= 0x1249249249249249ULL;
		x = (x ^ (x >> 2)) & 0x10c30c30c30c30c3ULL;
		x = (x ^ (x >> 4)) & 0x100f00f00f00f00fULL;
		x = (x ^ (x >> 8)) & 0x1f0000ff0000ffULL;
		x = (x ^ (x >> 16)) & 0x1f00000000ffffULL;
		x = (x ^ (x >> 32)) & 0x1fffffULL;
		return x;
	}

	/* =============================================================
	 * Encode / Decode (unsigned)
	 * ============================================================= */
	[[nodiscard]] static inline constexpr std::uint64_t Morton3D64(std::uint64_t x, std::uint64_t y, std::uint64_t z) noexcept {
		return (Part1By2_64(x) << 0) | (Part1By2_64(y) << 1) | (Part1By2_64(z) << 2);
	}

	struct Morton3DDecoded64 { std::uint64_t x, y, z; };

	[[nodiscard]] static inline constexpr Morton3DDecoded64 DeMorton3D64(std::uint64_t code) noexcept {
		return Morton3DDecoded64{
			Compact1By2_64(code >> 0),
			Compact1By2_64(code >> 1),
			Compact1By2_64(code >> 2)
		};
	}

	/* =============================================================
	 * Encode / Decode (signed via ZigZag)
	 * ============================================================= */
	[[nodiscard]] static inline constexpr std::uint64_t Morton3D64_ZZ(std::int64_t sx, std::int64_t sy, std::int64_t sz) noexcept {
		return Morton3D64(ZigZag64(sx), ZigZag64(sy), ZigZag64(sz));
	}

	[[nodiscard]] static inline constexpr Morton3DDecoded64 DeMorton3D64_ZZ(std::uint64_t code) noexcept {
		auto u = DeMorton3D64(code);
		return Morton3DDecoded64{
			static_cast<std::uint64_t>(UnZigZag64(u.x)),
			static_cast<std::uint64_t>(UnZigZag64(u.y)),
			static_cast<std::uint64_t>(UnZigZag64(u.z))
		};
	}

	/* =============================================================
	 * Convenience overloads for 32-bit signed inputs
	 * ============================================================= */
	[[nodiscard]] static inline constexpr std::uint64_t Morton3D64_ZZ(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
		return Morton3D64_ZZ(static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), static_cast<std::int64_t>(z));
	}
} // namespace SectorFW
