// sx_math.hpp
#pragma once
#include <type_traits>
#include <limits>
#include <cmath>
#include <cstdint>
#include <bit>
#include <algorithm>

namespace SectorFW::Math {
	//-------------------------------------
	// 基本コンセプト
	//-------------------------------------
	template<class T>
	concept Arithmetic = std::is_arithmetic_v<T>;

	template<class T>
	concept UnsignedInt = std::is_unsigned_v<T> && std::is_integral_v<T>;

	//-------------------------------------
	// 定数
	//-------------------------------------
	template<class T>
		requires std::is_floating_point_v<T>
	inline constexpr T pi_v = static_cast<T>(3.141592653589793238462643383279502884L);
	template<class T>
		requires std::is_floating_point_v<T>
	inline constexpr T tau_v = static_cast<T>(6.283185307179586476925286766559005768L);
	template<class T>
		requires std::is_floating_point_v<T>
	inline constexpr T half_pi_v = static_cast<T>(1.570796326794896619231321691639751442L);

	//-------------------------------------
	// 角度変換
	//-------------------------------------
	template<Arithmetic T, class R = std::conditional_t<std::is_integral_v<T>, double, T>>
	[[nodiscard]] constexpr R deg2rad(T deg) noexcept {
		return static_cast<R>(deg) * (pi_v<R> / R(180));
	}
	template<Arithmetic T, class R = std::conditional_t<std::is_integral_v<T>, double, T>>
	[[nodiscard]] constexpr R rad2deg(T rad) noexcept {
		return static_cast<R>(rad) * (R(180) / pi_v<R>);
	}

	//-------------------------------------
	// 比較・判定
	//-------------------------------------
	template<Arithmetic T>
	[[nodiscard]] constexpr bool is_finite(T x) noexcept {
		if constexpr (std::is_floating_point_v<T>) return std::isfinite(x);
		else return true;
	}

	// 近似等価: |a-b| <= max(abs_tol, rel_tol * max(|a|,|b|))
	template<Arithmetic T, class R = std::conditional_t<std::is_integral_v<T>, double, T>>
	[[nodiscard]] constexpr bool approx_equal(T a, T b,
		R rel_tol = R(1e-6),
		R abs_tol = R(0)) noexcept {
		const R ar = static_cast<R>(a), br = static_cast<R>(b);
		const R diff = std::abs(ar - br);
		const R limit = std::max(abs_tol, rel_tol * std::max(std::abs(ar), std::abs(br)));
		return diff <= limit;
	}

	//-------------------------------------
	// クランプ・補間・再マップ
	//-------------------------------------
	template<Arithmetic T>
	[[nodiscard]] constexpr T clamp(T x, T lo, T hi) noexcept {
		return (x < lo) ? lo : ((x > hi) ? hi : x);
	}
	template<Arithmetic T>
	[[nodiscard]] constexpr T clamp01(T x) noexcept { return clamp<T>(x, T(0), T(1)); }

	template<Arithmetic T>
	[[nodiscard]] constexpr T saturate(T x) noexcept {
		if constexpr (std::is_floating_point_v<T>) return clamp<T>(x, T(0), T(1));
		else return clamp<T>(x, T(0), T(1));
	}

	// 線形補間（非クランプ）。浮動小数はFMAを使用
	template<Arithmetic A, Arithmetic B, Arithmetic U>
	[[nodiscard]] constexpr auto lerp(A a, B b, U t) noexcept {
		using R = std::common_type_t<A, B, U>;
		const R ar = static_cast<R>(a), br = static_cast<R>(b), tr = static_cast<R>(t);
		if constexpr (std::is_floating_point_v<R>) {
			return std::fma(tr, (br - ar), ar); // ar + (br - ar) * tr
		}
		else {
			return ar + (br - ar) * tr;
		}
	}

	// クランプ付 lerp
	template<Arithmetic A, Arithmetic B, Arithmetic U>
	[[nodiscard]] constexpr auto lerp_clamped(A a, B b, U t) noexcept {
		using R = std::common_type_t<A, B, U>;
		return lerp(a, b, clamp01(static_cast<R>(t)));
	}

	// (a..b) → t  逆補間
	template<Arithmetic A, Arithmetic B, Arithmetic X>
	[[nodiscard]] constexpr auto inverse_lerp(A a, B b, X x) noexcept {
		using R = std::common_type_t<A, B, X>;
		const R ar = static_cast<R>(a), br = static_cast<R>(b), xr = static_cast<R>(x);
		const R denom = br - ar;
		if (denom == R(0)) return R(0);
		return (xr - ar) / denom;
	}

	// 区間再マップ (in0..in1)→(out0..out1)
	template<Arithmetic X, Arithmetic A, Arithmetic B, Arithmetic C, Arithmetic D>
	[[nodiscard]] constexpr auto remap(X x, A in0, B in1, C out0, D out1) noexcept {
		using R = std::common_type_t<X, A, B, C, D>;
		return lerp(static_cast<R>(out0), static_cast<R>(out1),
			inverse_lerp(static_cast<R>(in0), static_cast<R>(in1), static_cast<R>(x)));
	}
	template<Arithmetic X, Arithmetic A, Arithmetic B>
	[[nodiscard]] constexpr auto remap01(X x, A in0, B in1) noexcept {
		using R = std::common_type_t<X, A, B>;
		return clamp01(inverse_lerp(static_cast<R>(in0), static_cast<R>(in1), static_cast<R>(x)));
	}

	// ステップ・スムーズステップ
	template<Arithmetic E, Arithmetic X>
	[[nodiscard]] constexpr auto step(E edge, X x) noexcept {
		using R = std::common_type_t<E, X>;
		return (static_cast<R>(x) < static_cast<R>(edge)) ? R(0) : R(1);
	}
	template<Arithmetic E0, Arithmetic E1, Arithmetic X>
	[[nodiscard]] constexpr auto smoothstep(E0 e0, E1 e1, X x) noexcept {
		using R = std::common_type_t<E0, E1, X>;
		R t = remap01(static_cast<R>(x), static_cast<R>(e0), static_cast<R>(e1));
		return t * t * (R(3) - R(2) * t);
	}
	template<Arithmetic E0, Arithmetic E1, Arithmetic X>
	[[nodiscard]] constexpr auto smootherstep(E0 e0, E1 e1, X x) noexcept {
		using R = std::common_type_t<E0, E1, X>;
		R t = remap01(static_cast<R>(x), static_cast<R>(e0), static_cast<R>(e1));
		return t * t * t * (t * (t * R(6) - R(15)) + R(10)); // 6t^5 - 15t^4 + 10t^3
	}

	//-------------------------------------
	// ラップ・モジュロ
	//-------------------------------------
	template<Arithmetic X, Arithmetic A, Arithmetic B>
	[[nodiscard]] constexpr auto wrap(X x, A lo, B hi) noexcept {
		using R = std::common_type_t<X, A, B>;
		const R l = static_cast<R>(lo), h = static_cast<R>(hi);
		const R w = h - l;
		if constexpr (std::is_floating_point_v<R>) {
			R m = std::fmod(static_cast<R>(x) - l, w);
			if (m < R(0)) m += w;
			return m + l;
		}
		else {
			R v = static_cast<R>(x) - l;
			R m = v % w;
			if (m < R(0)) m += w;
			return m + l;
		}
	}
	template<Arithmetic X>
	[[nodiscard]] constexpr auto wrap01(X x) noexcept {
		return wrap(x, X(0), X(1));
	}

	//-------------------------------------
	// 角度ユーティリティ（ラジアン）
	//-------------------------------------
	template<std::floating_point T>
	[[nodiscard]] constexpr T wrap_angle_pi(T a) noexcept {            // (-π, π]
		return wrap(a, -pi_v<T>, pi_v<T>);
	}
	template<std::floating_point T>
	[[nodiscard]] constexpr T shortest_angle_delta(T from, T to) noexcept { // [-π, π]
		return wrap_angle_pi<T>(to - from);
	}

	//-------------------------------------
	// 符号・その他
	//-------------------------------------
	template<Arithmetic T>
	[[nodiscard]] constexpr int sign(T x) noexcept {
		if constexpr (std::is_signed_v<T> || std::is_floating_point_v<T>) {
			return (x > T(0)) - (x < T(0));
		}
		else {
			return x == T(0) ? 0 : 1;
		}
	}
	template<Arithmetic T>
	[[nodiscard]] constexpr T absdiff(T a, T b) noexcept {
		using R = std::common_type_t<T>;
		return static_cast<R>(a > b ? a - b : b - a);
	}

	//-------------------------------------
	// べき・ビット系
	//-------------------------------------
	template<UnsignedInt U>
	[[nodiscard]] constexpr bool is_power_of_two(U x) noexcept {
		return x && ((x & (x - 1)) == 0);
	}
	template<UnsignedInt U>
	[[nodiscard]] constexpr U ceil_pow2(U v) noexcept {
		if (v <= 1) return 1;
		v--;
		for (size_t s = 1; s < std::numeric_limits<U>::digits; s <<= 1) v |= (v >> s);
		return v + 1;
	}
	template<UnsignedInt U>
	[[nodiscard]] constexpr U floor_pow2(U v) noexcept {
		if (v == 0) return 0;
		return U(1) << (std::bit_width(v) - 1);
	}

	//-------------------------------------
	// アライン
	//-------------------------------------
	// 任意整列
	template<UnsignedInt U>
	[[nodiscard]] constexpr U align_up(U value, U alignment) noexcept {
		if (alignment == 0) return value;
		return (value + (alignment - 1)) / alignment * alignment;
	}
	template<UnsignedInt U>
	[[nodiscard]] constexpr U align_down(U value, U alignment) noexcept {
		if (alignment == 0) return value;
		return (value / alignment) * alignment;
	}
	// 2の冪整列（高速）
	template<UnsignedInt U>
	[[nodiscard]] constexpr U align_up_pow2(U value, U alignment_pow2) noexcept {
		return (value + (alignment_pow2 - 1)) & ~(alignment_pow2 - 1);
	}
	template<UnsignedInt U>
	[[nodiscard]] constexpr U align_down_pow2(U value, U alignment_pow2) noexcept {
		return value & ~(alignment_pow2 - 1);
	}

	//-------------------------------------
	// 安全な逆平方根（精度優先）
	//-------------------------------------
	template<std::floating_point T>
	[[nodiscard]] inline T rsqrt(T x) noexcept {
		return T(1) / std::sqrt(x);
	}

	static inline uint32_t LerpColor(uint32_t nearColor, uint32_t farColor, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);

		auto extract = [](uint32_t c, int shift) -> uint8_t {
			return (c >> shift) & 0xFF;
			};

		uint8_t nr = extract(nearColor, 24);
		uint8_t ng = extract(nearColor, 16);
		uint8_t nb = extract(nearColor, 8);
		uint8_t na = extract(nearColor, 0);

		uint8_t fr = extract(farColor, 24);
		uint8_t fg = extract(farColor, 16);
		uint8_t fb = extract(farColor, 8);
		uint8_t fa = extract(farColor, 0);

		auto lerp = [t](uint8_t a, uint8_t b) -> uint8_t {
			return static_cast<uint8_t>(a + (b - a) * t);
			};

		uint8_t r = lerp(nr, fr);
		uint8_t g = lerp(ng, fg);
		uint8_t b = lerp(nb, fb);
		uint8_t a = lerp(na, fa);

		return (r << 24) | (g << 16) | (b << 8) | a;
	}
} // namespace sx::math
