#pragma once

#include <cmath>
#include "Vector.hpp"

namespace SectorFW
{
    namespace Math
    {
        struct Quaternion {
            float x, y, z, w;

            // --- クォータニオンの正規化 ---
            void Normalize() {
                float len = std::sqrt(x * x + y * y + z * z + w * w);
                if (len > 0.0f) {
                    x /= len; y /= len; z /= len; w /= len;
                }
            }

            // --- オイラー角から作成（ラジアン）---
            static Quaternion FromEuler(float pitch, float yaw, float roll) {
                float cy = cos(yaw * 0.5f);
                float sy = sin(yaw * 0.5f);
                float cp = cos(pitch * 0.5f);
                float sp = sin(pitch * 0.5f);
                float cr = cos(roll * 0.5f);
                float sr = sin(roll * 0.5f);

                return {
                    sr * cp * cy - cr * sp * sy,
                    cr * sp * cy + sr * cp * sy,
                    cr * cp * sy - sr * sp * cy,
                    cr * cp * cy + sr * sp * sy
                };
            }

            // --- ベクトルを回転させる ---
            Vector3 RotateVector(const Vector3& v) const {
                // q * v * q^-1
                Quaternion vq{ v.x, v.y, v.z, 0 };
                Quaternion inv = this->Inverse();
                Quaternion result = (*this) * vq * inv;
                return { result.x, result.y, result.z };
            }

            // --- 逆クォータニオン ---
            Quaternion Inverse() const {
                return { -x, -y, -z, w }; // 単位クォータニオン前提
            }

            // --- SLERP 補間 ---
            static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
                float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
                Quaternion b2 = b;

                if (dot < 0.0f) { dot = -dot; b2 = { -b.x, -b.y, -b.z, -b.w }; }

                if (dot > 0.9995f) {
                    // 線形補間で近似
                    Quaternion result = {
                        a.x + t * (b2.x - a.x),
                        a.y + t * (b2.y - a.y),
                        a.z + t * (b2.z - a.z),
                        a.w + t * (b2.w - a.w)
                    };
                    result.Normalize();
                    return result;
                }

                float theta0 = acos(dot);
                float theta = theta0 * t;
                float sin_theta = sin(theta);
                float sin_theta0 = sin(theta0);

                float s0 = cos(theta) - dot * sin_theta / sin_theta0;
                float s1 = sin_theta / sin_theta0;

                return {
                    s0 * a.x + s1 * b2.x,
                    s0 * a.y + s1 * b2.y,
                    s0 * a.z + s1 * b2.z,
                    s0 * a.w + s1 * b2.w
                };
            }

            // --- 掛け算（回転の合成）---
            Quaternion operator*(const Quaternion& q) const {
                return {
                    w * q.x + x * q.w + y * q.z - z * q.y,
                    w * q.y - x * q.z + y * q.w + z * q.x,
                    w * q.z + x * q.y - y * q.x + z * q.w,
                    w * q.w - x * q.x - y * q.y - z * q.z
                };
            }
        };
    }
}
