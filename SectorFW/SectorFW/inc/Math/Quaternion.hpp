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

			Quat() :x(0), y(0), z(0), w(1) {}
			Quat(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}

			// --- クォータニオンの正規化 ---
			void Normalize() {
				T len = std::sqrt(x * x + y * y + z * z + w * w);
				if (len > 0.0f) {
					x /= len; y /= len; z /= len; w /= len;
				}
			}

			// --- オイラー角から作成（ラジアン）---
			static Quat FromEuler(T pitch, T yaw, T roll) {
				T cy = cos(yaw * 0.5f);
				T sy = sin(yaw * 0.5f);
				T cp = cos(pitch * 0.5f);
				T sp = sin(pitch * 0.5f);
				T cr = cos(roll * 0.5f);
				T sr = sin(roll * 0.5f);

				return {
					sr * cp * cy - cr * sp * sy,
					cr * sp * cy + sr * cp * sy,
					cr * cp * sy - sr * sp * cy,
					cr * cp * cy + sr * sp * sy
				};
			}

			// --- ベクトルを回転させる ---
			Vec3f RotateVector(const Vec3f& v) const {
				// q * v * q^-1
				Quat vq{ v.x, v.y, v.z, 0 };
				Quat inv = this->Inverse();
				Quat result = (*this) * vq * inv;
				return { result.x, result.y, result.z };
			}

			// --- 逆クォータニオン ---
			Quat Inverse() const {
				return { -x, -y, -z, w }; // 単位クォータニオン前提
			}

			// --- SLERP 補間 ---
			static Quat Slerp(const Quat& a, const Quat& b, T t) {
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

			// --- 掛け算（回転の合成）---
			Quat operator*(const Quat& q) const {
				return {
					w * q.x + x * q.w + y * q.z - z * q.y,
					w * q.y - x * q.z + y * q.w + z * q.x,
					w * q.z + x * q.y - y * q.x + z * q.w,
					w * q.w - x * q.x - y * q.y - z * q.z
				};
			}
		};

		using Quatf = Quat<float>;
	}
}