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

namespace SFW
{
	namespace Math
	{
		template<typename T>
		struct Quat {
			union {
				struct { T x, y, z, w; };
				T v[4];
			};

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

			static Quat Identity() noexcept { return Quat(0, 0, 0, 1); }

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

			// q = swing * twist となるように、
			// 指定軸 axis 周りの回転(twist)と、それ以外の回転(swing)に分解する。
			// axis はワールド/ローカルどちらの軸かは呼び出し側の解釈次第。
			// 単位クォータニオン前提（不安なら Normalize() してから呼ぶ）
			void DecomposeSwingTwist(const Vec3<T>& axis,
				Quat& outSwing,
				Quat& outTwist) const noexcept
			{
				// axis を正規化
				Vec3<T> n = axis;
				{
					const T len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
					const T eps = std::numeric_limits<T>::epsilon();
					if (len <= eps) {
						// 軸がゼロベクトルなら分解不能なので、
						// twist = identity, swing = this として返す
						outTwist = Quat(T(0), T(0), T(0), T(1));
						outSwing = *this;
						return;
					}
					const T invLen = T(1) / len;
					n.x *= invLen; n.y *= invLen; n.z *= invLen;
				}

				// q のベクトル部
				const Vec3<T> v{ x, y, z };

				// v を軸 n に射影
				const T projLen = n.x * v.x + n.y * v.y + n.z * v.z;
				const Vec3<T> proj{ n.x * projLen, n.y * projLen, n.z * projLen };

				// twist: スカラー部はそのまま、ベクトル部を axis 方向だけにしたクォータニオン
				Quat twist{ proj.x, proj.y, proj.z, w };

				// proj がほぼゼロなら「軸周りの回転はほぼ無い」とみなして identity
				const T projLenSq = proj.x * proj.x + proj.y * proj.y + proj.z * proj.z;
				if (projLenSq <= std::numeric_limits<T>::epsilon()) {
					twist = Quat(T(0), T(0), T(0), T(1));
				}
				else {
					twist.Normalize();
				}

				// swing = q * conj(twist)
				const Quat conjTwist(-twist.x, -twist.y, -twist.z, twist.w);
				Quat swing = (*this) * conjTwist;
				swing.Normalize();

				outSwing = swing;
				outTwist = twist;
			}

			// 指定軸まわりの回転成分（twist）だけを残す
			Quat KeepTwist(const Vec3<T>& axis) const noexcept
			{
				Quat swing, twist;
				DecomposeSwingTwist(axis, swing, twist);
				return twist;
			}

			// 指定軸 axis 周りの回転成分(twist)だけを除去したクォータニオンを返す。
			// -> swing だけを返す
			Quat RemoveTwist(const Vec3<T>& axis) const noexcept
			{
				Quat swing, twist;
				DecomposeSwingTwist(axis, swing, twist);
				return swing;
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

		// --- 小ヘルパー -------------------------------------------------
		template<typename T>
		inline Vec3<T> NormalizeSafe(const Vec3<T>& v) noexcept {
			const T l2 = v.x * v.x + v.y * v.y + v.z * v.z;
			if (l2 <= std::numeric_limits<T>::epsilon()) return { T(0),T(0),T(0) };
			const T inv = T(1) / std::sqrt(l2);
			return { v.x * inv, v.y * inv, v.z * inv };
		}

		template<typename T>
		inline void OrthonormalizeBasis(Basis<T>& b) noexcept {
			// r を正規化
			Vec3<T> r = NormalizeSafe(b.right);

			// u から r 成分を引いて正規化（Gram–Schmidt）
			Vec3<T> u = {
				b.up.x - (r.x * (b.up.x * r.x + b.up.y * r.y + b.up.z * r.z)),
				b.up.y - (r.y * (b.up.x * r.x + b.up.y * r.y + b.up.z * r.z)),
				b.up.z - (r.z * (b.up.x * r.x + b.up.y * r.y + b.up.z * r.z)),
			};
			u = NormalizeSafe(u);

			// f は r×u から作る（右手系）。入力 f と反転していたら符号合わせ
			Vec3<T> f = {
				r.y * u.z - r.z * u.y,
				r.z * u.x - r.x * u.z,
				r.x * u.y - r.y * u.x
			};
			// 入力 forward と向きが逆なら反転
			T s = (f.x * b.forward.x + f.y * b.forward.y + f.z * b.forward.z) < T(0) ? T(-1) : T(1);
			f = { f.x * s, f.y * s, f.z * s };

			b.right = r; b.up = u; b.forward = f;
		}

		// --- Basis(+Z列) → Quaternion（数値安定版） ----------------------
		// ※ Basis.forward は「+Z列」（FastBasisFromQuat と同じ定義）を期待
		template<typename T>
		inline Quat<T> QuatFromBasisPlusZ(Basis<T> b) noexcept {
			// 入力の微妙な非直交・非単位を補正
			OrthonormalizeBasis(b);

			// 列ベクトルを 3x3 行列に配置（FastBasisFromQuat と往復一致させる）
			// R = [ r u f ]（列）
			const T m00 = b.right.x, m01 = b.up.x, m02 = b.forward.x;
			const T m10 = b.right.y, m11 = b.up.y, m12 = b.forward.y;
			const T m20 = b.right.z, m21 = b.up.z, m22 = b.forward.z;

			const T trace = m00 + m11 + m22;
			Quat<T> q;

			if (trace > T(0)) {
				T s = std::sqrt(trace + T(1)) * T(2);
				q.w = T(0.25) * s;
				q.x = (m21 - m12) / s;
				q.y = (m02 - m20) / s;
				q.z = (m10 - m01) / s;
			}
			else if (m00 > m11 && m00 > m22) {
				T s = std::sqrt(T(1) + m00 - m11 - m22) * T(2);
				q.w = (m21 - m12) / s;
				q.x = T(0.25) * s;
				q.y = (m01 + m10) / s;
				q.z = (m02 + m20) / s;
			}
			else if (m11 > m22) {
				T s = std::sqrt(T(1) + m11 - m00 - m22) * T(2);
				q.w = (m02 - m20) / s;
				q.x = (m01 + m10) / s;
				q.y = T(0.25) * s;
				q.z = (m12 + m21) / s;
			}
			else {
				T s = std::sqrt(T(1) + m22 - m00 - m11) * T(2);
				q.w = (m10 - m01) / s;
				q.x = (m02 + m20) / s;
				q.y = (m12 + m21) / s;
				q.z = T(0.25) * s;
			}

			// 丸め誤差対策
			q.Normalize();
			return q;
		}

		template<typename T, typename Convention = LH_ZForward>
		inline Quat<T> QuatFromBasis(const Vec3<T>& right,
			const Vec3<T>& up,
			const Vec3<T>& forward) noexcept
		{
			Basis<T> b;
			b.right = right;
			b.up = up;

			if constexpr (std::is_same_v<Convention, RH_ZBackward>) {
				// アプリの forward(-Z) → 内部の +Z 列へ
				b.forward = { -forward.x, -forward.y, -forward.z };
			}
			else { // LH_ZForward など（+Z が前）
				b.forward = forward;
			}
			return QuatFromBasisPlusZ(b);
		}

		// --- 便利なオーバーロード（Basis をそのまま渡す版） --------------
		// 既に forward が「+Z 列」になっている Basis を持っている場合はこちら
		template<typename T>
		inline Quat<T> QuatFromBasis(const Basis<T>& basisPlusZ) noexcept {
			return QuatFromBasisPlusZ(basisPlusZ);
		}
	}
}