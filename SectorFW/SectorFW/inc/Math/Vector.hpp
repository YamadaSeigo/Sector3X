#pragma once

#include <cmath>
#include <cassert>
#include <iostream>

namespace SectorFW
{
    namespace Math
    {
        template<typename T>
        struct Vec2 {
            union {
                struct { T x, y; };
                T data[2];
            };

            Vec2() : x(0), y(0) {}
            Vec2(T x_, T y_) : x(x_), y(y_) {}
            explicit Vec2(T val) : x(val), y(val) {}

            T& operator[](size_t i) { return data[i]; }
            const T& operator[](size_t i) const { return data[i]; }

            Vec2 operator+(const Vec2& rhs) const { return Vec2(x + rhs.x, y + rhs.y); }
            Vec2 operator-(const Vec2& rhs) const { return Vec2(x - rhs.x, y - rhs.y); }
            Vec2 operator*(T s) const { return Vec2(x * s, y * s); }

            T dot(const Vec2& rhs) const { return x * rhs.x + y * rhs.y; }

            T length() const { return std::sqrt(dot(*this)); }

            Vec2 normalized() const {
                T len = length();
                assert(len != 0);
                return *this * (T(1) / len);
            }
        };

        template<typename T>
        struct Vec3 {
            union {
                struct { T x, y, z; };
                T data[3];
            };

            Vec3() : x(0), y(0), z(0) {}
            Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
            explicit Vec3(T val) : x(val), y(val), z(val) {}

            T& operator[](size_t i) { return data[i]; }
            const T& operator[](size_t i) const { return data[i]; }

            Vec3 operator+(const Vec3& rhs) const { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
            Vec3 operator-(const Vec3& rhs) const { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
            Vec3 operator*(T s) const { return Vec3(x * s, y * s, z * s); }

            T dot(const Vec3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }

            T length() const { return std::sqrt(dot(*this)); }

            Vec3 normalized() const {
                T len = length();
                assert(len != 0);
                return *this * (T(1) / len);
            }

            Vec3 cross(const Vec3& rhs) const {
                return Vec3(
                    y * rhs.z - z * rhs.y,
                    z * rhs.x - x * rhs.z,
                    x * rhs.y - y * rhs.x
                );
            }
        };

        template<typename T>
        struct Vec4 {
            union {
                struct { T x, y, z, w; };
                T data[4];
            };

            Vec4() : x(0), y(0), z(0), w(0) {}
            Vec4(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
            explicit Vec4(T val) : x(val), y(val), z(val), w(val) {}

            T& operator[](size_t i) { return data[i]; }
            const T& operator[](size_t i) const { return data[i]; }

            Vec4 operator+(const Vec4& rhs) const { return Vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w); }
            Vec4 operator-(const Vec4& rhs) const { return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w); }
            Vec4 operator*(T s) const { return Vec4(x * s, y * s, z * s, w * s); }

            T dot(const Vec4& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w; }

            T length() const { return std::sqrt(dot(*this)); }

            Vec4 normalized() const {
                T len = length();
                assert(len != 0);
                return *this * (T(1) / len);
            }
        };

        template<typename T, typename U>
        inline T lerp(const T& a, const T& b, U t) {
            return a * (U(1) - t) + b * t;
        }

        template<typename T>
        inline T smoothstep(T edge0, T edge1, T x) {
            T t = std::clamp((x - edge0) / (edge1 - edge0), T(0), T(1));
            return t * t * (T(3) - T(2) * t);
        }

        template<typename T>
        inline T step(T edge, T x) {
            return x < edge ? T(0) : T(1);
        }

        template<typename VecT>
        inline bool any(const VecT& v) {
            for (size_t i = 0; i < sizeof(v.data) / sizeof(v.data[0]); ++i)
                if (v[i]) return true;
            return false;
        }

        template<typename VecT>
        inline bool all(const VecT& v) {
            for (size_t i = 0; i < sizeof(v.data) / sizeof(v.data[0]); ++i)
                if (!v[i]) return false;
            return true;
        }

        using Vector2 = Vec2<float>;
        using Vector3 = Vec3<float>;
        using Vector4 = Vec4<float>;
    }
}