/*****************************************************************//**
 * @file   Vector.hpp
 * @brief ベクトルを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <cmath>
#include <cassert>
#include <iostream>
#include <algorithm>

#include <xmmintrin.h> // SSE

namespace SectorFW
{
	namespace Math
	{
		template<typename T, size_t N>
		constexpr size_t GetAlignmentForVector() noexcept {
			if constexpr (std::is_same_v<T, float>) {
				return N * sizeof(T) >= 16 ? 16 : alignof(T);
			}
			else if constexpr (std::is_same_v<T, double>) {
				return N * sizeof(T) >= 32 ? 32 : alignof(T);
			}
			else {
				return alignof(T);
			}
		}

		template<typename T>
		struct alignas(GetAlignmentForVector<T, 2>()) Vec2 {
			union {
				struct { T x, y; };
				T data[2];
			};

			Vec2() noexcept : x(0), y(0) {}
			Vec2(T x_, T y_) noexcept : x(x_), y(y_) {}
			explicit Vec2(T val) noexcept : x(val), y(val) {}

			T& operator[](size_t i) noexcept { return data[i]; }
			const T& operator[](size_t i) const noexcept { return data[i]; }

			Vec2 operator+(const Vec2& rhs) const noexcept { return Vec2(x + rhs.x, y + rhs.y); }
			Vec2 operator-(const Vec2& rhs) const noexcept { return Vec2(x - rhs.x, y - rhs.y); }
			Vec2 operator*(T s) const noexcept { return Vec2(x * s, y * s); }

			T dot(const Vec2& rhs) const noexcept { return x * rhs.x + y * rhs.y; }

			T length() const noexcept { return std::sqrt(dot(*this)); }

			Vec2 normalized() const noexcept {
				T len = length();
				assert(len != 0);
				return *this * (T(1) / len);
			}
		};

		template<typename T>
		struct alignas(GetAlignmentForVector<T, 3>()) Vec3 {
			union {
				struct { T x, y, z; };
				T data[3];
			};

			Vec3() noexcept : x(0), y(0), z(0) {}
			Vec3(T x_, T y_, T z_) noexcept : x(x_), y(y_), z(z_) {}
			explicit Vec3(T val) noexcept : x(val), y(val), z(val) {}

			T& operator[](size_t i) noexcept { return data[i]; }
			const T& operator[](size_t i) const noexcept { return data[i]; }

			Vec3& operator=(const Vec3& rhs) noexcept {
				if (this != &rhs) {
					x = rhs.x; y = rhs.y; z = rhs.z;
				}
				return *this;
			}
			Vec3& operator=(T&& val) noexcept {
				x = val; y = val; z = val;
				return *this;
			}
			Vec3& operator=(const T& val) noexcept {
				x = val; y = val; z = val;
				return *this;
			}

			Vec3 operator+(const Vec3& rhs) const noexcept { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
			Vec3 operator+(const T& s) const noexcept { return Vec3(x + s, y + s, z + s); }
			Vec3& operator+=(const Vec3& rhs) noexcept { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
			Vec3& operator+=(const T& s) noexcept { x += s; y += s; z += s; return *this; }
			Vec3 operator-(const Vec3& rhs) const noexcept { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
			Vec3 operator-(const T& s) const noexcept { return Vec3(x - s, y - s, z - s); }
			Vec3& operator-=(const Vec3& rhs) noexcept { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
			Vec3& operator-=(const T& s) noexcept { x -= s; y -= s; z -= s; return *this; }
			Vec3 operator*(const Vec3& rhs) const noexcept { return Vec3(x * rhs.x, y * rhs.y, z * rhs.z); }
			Vec3 operator*(T s) const noexcept { return Vec3(x * s, y * s, z * s); }
			Vec3& operator*=(const Vec3& rhs) noexcept { x *= rhs.x; y *= rhs.y; z *= rhs.z; return *this; }
			Vec3& operator*=(T s) noexcept { x *= s; y *= s; z *= s; return *this; }
			Vec3 operator/(const Vec3& rhs) const noexcept { assert(rhs.x != 0 && rhs.y != 0 && rhs.z != 0); return Vec3(x / rhs.x, y / rhs.y, z / rhs.z); }
			Vec3 operator/(T s) const noexcept { assert(s != 0); return Vec3(x / s, y / s, z / s); }
			Vec3& operator/=(const Vec3& rhs) noexcept { assert(rhs.x != 0 && rhs.y != 0 && rhs.z != 0); x /= rhs.x; y /= rhs.y; z /= rhs.z; return *this; }
			Vec3& operator/=(T s) noexcept { assert(s != 0); x /= s; y /= s; z /= s; return *this; }

			T dot(const Vec3& rhs) const noexcept { return x * rhs.x + y * rhs.y + z * rhs.z; }

			T length() const noexcept { return std::sqrt(dot(*this)); }

			Vec3 normalized() const noexcept {
				T len = length();
				assert(len != 0);
				return *this * (T(1) / len);
			}

			Vec3 cross(const Vec3& rhs) const noexcept {
				return Vec3(
					y * rhs.z - z * rhs.y,
					z * rhs.x - x * rhs.z,
					x * rhs.y - y * rhs.x
				);
			}
		};

		// 規約タグ
		struct RH_ZBackward {}; // 例: OpenGL風 (右手系, +Zが奥 → forward = (0,0,-1))
		struct LH_ZForward {}; // 例: DirectX LH風 (左手系, +Zが前 → forward = (0,0,+1))

		template<typename T, typename Convention = RH_ZBackward>
		struct Axes {
			// 基本軸（規約で変わるのは forward と cross の順序）
			static constexpr Vec3<T> up()       noexcept { return { T(0), T(1), T(0) }; }
			static constexpr Vec3<T> down()     noexcept { return { T(0), T(-1), T(0) }; }
			static constexpr Vec3<T> right()    noexcept { return { T(1), T(0), T(0) }; } // 右方向の+Xは固定
			static constexpr Vec3<T> left()     noexcept { return { T(-1), T(0), T(0) }; }

			static constexpr Vec3<T> forward()  noexcept {
				if constexpr (std::is_same_v<Convention, RH_ZBackward>) return { T(0), T(0), T(-1) };
				else                                                    return { T(0), T(0), T(1) }; // LH_ZForward
			}
			static constexpr Vec3<T> back()     noexcept { auto f = forward(); return Vec3<T>{-f.x, -f.y, -f.z}; }

			// 与えた forward / up から「右」(正規直交基底) を作る：クロス積の順序が規約で変わる
			static Vec3<T> makeRight(const Vec3<T>& up, const Vec3<T>& forward) noexcept {
				if constexpr (std::is_same_v<Convention, RH_ZBackward>) {
					return up.cross(forward).normalized();     // 右手系: right = up × forward
				}
				else {
					return forward.cross(up).normalized();     // 左手系: right = forward × up
				}
			}

			// 与えた up と right から forward を作る（数値安定のため再直交化にも使える）
			static Vec3<T> makeForward(const Vec3<T>& up, const Vec3<T>& right) noexcept {
				if constexpr (std::is_same_v<Convention, RH_ZBackward>) {
					return right.cross(up).normalized();       // 右手系: forward = right × up
				}
				else {
					return up.cross(right).normalized();       // 左手系: forward = up × right
				}
			}
		};

		using RFAxes = Axes<float, RH_ZBackward>; // OpenGL風（右手系, forward = -Z）
		using LFAxes = Axes<float, LH_ZForward>; // OpenGL風（左手系, forward = +Z）

		template<typename T>
		struct alignas(GetAlignmentForVector<T, 4>()) Vec4 {
			union {
				struct { T x, y, z, w; };
				T data[4];
			};

			Vec4() noexcept : x(0), y(0), z(0), w(0) {}
			Vec4(T x_, T y_, T z_, T w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
			explicit Vec4(T val) noexcept : x(val), y(val), z(val), w(val) {}

			T& operator[](size_t i) noexcept { return data[i]; }
			const T& operator[](size_t i) const noexcept { return data[i]; }

			Vec4 operator+(const Vec4& rhs) const noexcept { return Vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w); }
			Vec4 operator-(const Vec4& rhs) const noexcept { return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w); }
			Vec4 operator*(T s) const noexcept { return Vec4(x * s, y * s, z * s, w * s); }

			T dot(const Vec4& rhs) const noexcept { return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w; }

			T length() const noexcept { return std::sqrt(dot(*this)); }

			Vec4 normalized() const noexcept {
				T len = length();
				assert(len != 0);
				return *this * (T(1) / len);
			}
		};

		template<typename VecT>
		inline bool any(const VecT& v) noexcept {
			for (size_t i = 0; i < sizeof(v.data) / sizeof(v.data[0]); ++i)
				if (v[i]) return true;
			return false;
		}

		template<typename VecT>
		inline bool all(const VecT& v) noexcept {
			for (size_t i = 0; i < sizeof(v.data) / sizeof(v.data[0]); ++i)
				if (!v[i]) return false;
			return true;
		}

		using Vec2f = Vec2<float>;
		using Vec3f = Vec3<float>;
		using Vec4f = Vec4<float>;

		template<>
		inline Vec4f Vec4f::operator+(const Vec4f& rhs) const noexcept {
			__m128 a = _mm_load_ps(this->data);     // this の4要素をロード
			__m128 b = _mm_load_ps(rhs.data);       // rhs の4要素をロード
			__m128 result = _mm_add_ps(a, b);       // SIMD加算
			Vec4f out;
			_mm_store_ps(out.data, result);         // 結果を保存
			return out;
		}

		template<>
		inline float Vec4f::dot(const Vec4f& rhs) const noexcept {
			__m128 a = _mm_load_ps(this->data);
			__m128 b = _mm_load_ps(rhs.data);
			__m128 mul = _mm_mul_ps(a, b);
			__m128 shuf = _mm_movehdup_ps(mul);      // 高位を複製
			__m128 sums = _mm_add_ps(mul, shuf);
			shuf = _mm_movehl_ps(shuf, sums);        // 上位に詰める
			sums = _mm_add_ss(sums, shuf);
			return _mm_cvtss_f32(sums);              // 結果をfloatに
		}
	}
}