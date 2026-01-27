#pragma once

#include <cassert>
#include <array>

struct Packed2Bits32 {
	static constexpr std::size_t kCapacity = 16; // 16個入る（2bit * 16 = 32bit）

	std::uint32_t data = 0;

	// i番目(0..15)に値v(0..3)をセット
	void set(std::size_t i, std::uint8_t v) {
		assert(i < kCapacity);
		assert(v < 4);
		const std::uint32_t shift = static_cast<std::uint32_t>(i * 2);
		const std::uint32_t mask = (0x3u << shift);
		data = (data & ~mask) | (static_cast<std::uint32_t>(v) << shift);
	}

	// i番目(0..15)を取得
	std::uint8_t get(std::size_t i) const {
		assert(i < kCapacity);
		const std::uint32_t shift = static_cast<std::uint32_t>(i * 2);
		return static_cast<std::uint8_t>((data >> shift) & 0x3u);
	}

	// 配列からまとめて詰める（16要素）
	void pack(const std::array<std::uint8_t, kCapacity>& src) {
		std::uint32_t tmp = 0;
		for (std::size_t i = 0; i < kCapacity; ++i) {
			assert(src[i] < 4);
			tmp |= (static_cast<std::uint32_t>(src[i]) << (i * 2));
		}
		data = tmp;
	}

	// まとめて展開
	void unpack(std::array<std::uint8_t, kCapacity>& dst) const {
		for (std::size_t i = 0; i < kCapacity; ++i) {
			dst[i] = get(i);
		}
	}
};