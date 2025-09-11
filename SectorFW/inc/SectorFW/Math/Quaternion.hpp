/*****************************************************************//**
 * @file   Quaternion.hpp
 * @brief クォータニオンを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <cmath>
#include <limits>
#include <algorithm>
#include "Vector.hpp"

namespace SectorFW
{
	namespace Math
	{
		template<typename T>
		struct alignas(sizeof(T) == 8 ? 32 : 16) Quat {
			T x, y, z, w;

			Quat() noexcept : x(0), y(0), z(0), w(1) {}
			Quat(T x, T y, T z, T w) noexcept : x(x), y(y), z(z), w(w) {}

			// 長さ/正規化
			T LengthSq() const noexcept { return x * x + y * y + z * z + w * w; }

			void Normalize() noexcept {
				const T len2 = LengthSq();
				const T eps = std::numeric_limits<T>::epsilon();
				if (len2 <= eps) { x = 0; y = 0; z = 0; w = 1; return; }
				const T inv = T(1) / std::sqrt(len2);
				x *= inv; y *= inv; z *= inv; w *= inv;
			}
			Quat Normalized() const noexcept { Quat q = *this; q.Normalize(); return q; }

			// オイラー（ピッチ= X, ヨー= Y, ロール= Z : 適用順は Yaw→Pitch→Roll を想定）
			static Quat FromEuler(T pitch, T yaw, T roll) noexcept {
				const T h = T(0.5);
				const T cy = std::cos(yaw * h), sy = std::sin(yaw * h);
				const T cp = std::cos(pitch * h), sp = std::sin(pitch * h);
				const T cr = std::cos(roll * h), sr = std::sin(roll * h);

				// （注）この式は Y->X->Z の順（右掛け適用）を想定
				return Quat(
					sr * cp * cy - cr * sp * sy,
					cr * sp * cy + sr * cp * sy,
					cr * cp * sy - sr * sp * cy,
					cr * cp * cy + sr * sp * sy
				);
			}

			static Quat FromAxisAngle(const Vec3<T>& axis, T angleRad) noexcept {
				const T len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
				if (len <= std::numeric_limits<T>::epsilon()) return Quat(); // identity
				const T inv = T(1) / len;
				const T half = angleRad * T(0.5);
				const T s = std::sin(half), c = std::cos(half);
				return Quat(axis.x * inv * s, axis.y * inv * s, axis.z * inv * s, c);
			}

			// ベクトル回転（単位 Q 前提）
			Vec3<T> RotateVector(const Vec3<T>& v) const noexcept {
				const Vec3<T> qv{ x,y,z };
				const Vec3<T> t = qv.cross(v) * T(2);
				return v + t * w + qv.cross(t);
			}

			Quat Inverse() const noexcept { return Quat(-x, -y, -z, w); } // unit 前提

			static Quat Slerp(const Quat& a, const Quat& b, T t) noexcept {
				T dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
				Quat b2 = b;
				if (dot < T(0)) { dot = -dot; b2 = Quat(-b.x, -b.y, -b.z, -b.w); }

				// 丸め誤差対策
				dot = std::clamp(dot, T(-1), T(1));

				if (dot > T(0.9995)) {
					Quat r{ a.x + t * (b2.x - a.x),
							a.y + t * (b2.y - a.y),
							a.z + t * (b2.z - a.z),
							a.w + t * (b2.w - a.w) };
					r.Normalize();
					return r;
				}

				const T theta0 = std::acos(dot);
				const T theta = theta0 * t;
				const T s0 = std::cos(theta) - dot * std::sin(theta) / std::sin(theta0);
				const T s1 = std::sin(theta) / std::sin(theta0);
				return Quat(s0 * a.x + s1 * b2.x,
					s0 * a.y + s1 * b2.y,
					s0 * a.z + s1 * b2.z,
					s0 * a.w + s1 * b2.w);
			}

			// （右掛け合成）「this の後に q を適用」
			Quat operator*(const Quat& q) const noexcept {
				return Quat(
					w * q.x + x * q.w + y * q.z - z * q.y,
					w * q.y - x * q.z + y * q.w + z * q.x,
					w * q.z + x * q.y - y * q.x + z * q.w,
					w * q.w - x * q.x - y * q.y - z * q.z
				);
			}
		};

		using Quatf = Quat<float>;

		template<typename T>
		struct Basis {
			Vec3<T> right;
			Vec3<T> up;
			Vec3<T> forward; // ここは「+Z を回した結果」。規約で必要なら後で符号反転
		};

		// q は単位クォータニオン前提（不安なら Normalize() してから呼ぶ）
		template<typename T>
		inline Basis<T> FastBasisFromQuat(const Quat<T>& q) noexcept {
			const T x = q.x, y = q.y, z = q.z, w = q.w;

			const T xx = x + x, yy = y + y, zz = z + z;
			const T wx = w * xx, wy = w * yy, wz = w * zz;
			const T xx2 = x * xx, xy2 = x * yy, xz2 = x * zz;
			const T yy2 = y * yy, yz2 = y * zz, zz2 = z * zz;

			Basis<T> b;
			// 列ベクトル（Right, Up, Forward(+Z)）
			b.right = { T(1) - (yy2 + zz2),  xy2 + wz,           xz2 - wy };
			b.up = { xy2 - wz,     T(1) - (xx2 + zz2), yz2 + wx };
			b.forward = { xz2 + wy,            yz2 - wx,    T(1) - (xx2 + yy2) };
			return b;
		}

		// 規約に合わせた最終的な基底
		template<typename T, typename Convention = RH_ZBackward>
		inline void ToBasis(const Quat<T>& q, Vec3<T>& outRight, Vec3<T>& outUp, Vec3<T>& outForward) noexcept
		{
			Basis<T> b = FastBasisFromQuat(q);

			outRight = b.right;
			outUp = b.up;

			if constexpr (std::is_same_v<Convention, RH_ZBackward>) {
				// RH_ZBackward: forward は -Z 基準 → +Z 列を反転
				outForward = { -b.forward.x, -b.forward.y, -b.forward.z };
			}
			else {
				// LH_ZForward: +Z が前
				outForward = b.forward;
			}
		}

		// 個別取得ショートカット
		template<typename T, typename Convention = RH_ZBackward>
		inline Vec3<T> QuatRight(const Quat<T>& q)   noexcept { Vec3<T> r, u, f; ToBasis<T, Convention>(q, r, u, f); return r; }
		template<typename T, typename Convention = RH_ZBackward>
		inline Vec3<T> QuatUp(const Quat<T>& q)      noexcept { Vec3<T> r, u, f; ToBasis<T, Convention>(q, r, u, f); return u; }
		template<typename T, typename Convention = RH_ZBackward>
		inline Vec3<T> QuatForward(const Quat<T>& q) noexcept { Vec3<T> r, u, f; ToBasis<T, Convention>(q, r, u, f); return f; }
	}
}