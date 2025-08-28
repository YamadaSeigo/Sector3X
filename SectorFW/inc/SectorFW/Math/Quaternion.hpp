/*****************************************************************//**
 * @file   Quaternion.hpp
 * @brief クォータニオンを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <cmath>
#include "Vector.hpp"

namespace SectorFW
{
	namespace Math
	{
		template<typename T>
		struct alignas(sizeof(T) == 8 ? 32 : 16) Quat {
			T x, y, z, w;

			Quat() noexcept :x(0), y(0), z(0), w(1) {}
			explicit Quat(T x, T y, T z, T w) noexcept : x(x), y(y), z(z), w(w) {}

			// --- クォータニオンの正規化 ---
			void Normalize() noexcept {
				T len = std::sqrt(x * x + y * y + z * z + w * w);
				if (len > 0.0f) {
					x /= len; y /= len; z /= len; w /= len;
				}
			}

			// --- オイラー角から作成（ラジアン）---
			static Quat FromEuler(T pitch, T yaw, T roll) noexcept {
				T cy = cos(yaw * 0.5f);
				T sy = sin(yaw * 0.5f);
				T cp = cos(pitch * 0.5f);
				T sp = sin(pitch * 0.5f);
				T cr = cos(roll * 0.5f);
				T sr = sin(roll * 0.5f);

				return Quat(
					sr * cp * cy - cr * sp * sy,
					cr * sp * cy + sr * cp * sy,
					cr * cp * sy - sr * sp * cy,
					cr * cp * cy + sr * sp * sy
				);
			}

			static Quat FromAxisAngle(const Vec3<T>& axis, T angleRad) noexcept {
				Vec3<T> normAxis = axis.normalized();
				T halfAngle = angleRad * T(0.5);
				T sinHalf = sin(halfAngle);
				T cosHalf = cos(halfAngle);

				return Quat(
					normAxis.x * sinHalf,
					normAxis.y * sinHalf,
					normAxis.z * sinHalf,
					cosHalf
				);
			}

			// --- ベクトルを回転させる ---
			Vec3<T> RotateVector(const Vec3<T>& v) const noexcept {
				// this は単位クォータニオンを想定
				Vec3<T> qv{ x, y, z };
				Vec3<T> t = qv.cross(v) * T(2);              // t = 2 * (qv × v)
				return v + t * w + qv.cross(t);              // v' = v + w*t + (qv × t)
			}

			// --- 逆クォータニオン ---
			Quat Inverse() const noexcept {
				return Quat(-x, -y, -z, w); // 単位クォータニオン前提
			}

			// --- SLERP 補間 ---
			static Quat Slerp(const Quat& a, const Quat& b, T t) noexcept {
				T dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
				Quat b2 = b;

				if (dot < 0.0f) { dot = -dot; b2 = { -b.x, -b.y, -b.z, -b.w }; }

				if (dot > 0.9995f) {
					// 線形補間で近似
					Quat result = {
						a.x + t * (b2.x - a.x),
						a.y + t * (b2.y - a.y),
						a.z + t * (b2.z - a.z),
						a.w + t * (b2.w - a.w)
					};
					result.Normalize();
					return result;
				}

				T theta0 = acos(dot);
				T theta = theta0 * t;
				T sin_theta = sin(theta);
				T sin_theta0 = sin(theta0);

				T s0 = cos(theta) - dot * sin_theta / sin_theta0;
				T s1 = sin_theta / sin_theta0;

				return {
					s0 * a.x + s1 * b2.x,
					s0 * a.y + s1 * b2.y,
					s0 * a.z + s1 * b2.z,
					s0 * a.w + s1 * b2.w
				};
			}

			Quat& operator=(const Quat& other) noexcept {
				if (this != &other) {
					x = other.x; y = other.y; z = other.z; w = other.w;
				}
				return *this;
			}

			// --- 掛け算（回転の合成）---
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