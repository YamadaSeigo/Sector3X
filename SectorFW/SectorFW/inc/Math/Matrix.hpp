/*****************************************************************//**
 * @file   Matrix.hpp
 * @brief 行列を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <array>
#include <cassert>

#include "Vector.hpp"
#include "Quaternion.hpp"

namespace SectorFW
{
	namespace Math
	{
		template <size_t Rows, size_t Cols, typename T>
		struct Matrix {
			static constexpr size_t Rows = Rows;
			static constexpr size_t Cols = Cols;

			std::array<std::array<T, Cols>, Rows> m{};

			// 単位行列（正方行列のみ）
			static Matrix Identity() noexcept {
				static_assert(Rows == Cols, "Identity matrix must be square.");
				Matrix mat;
				for (size_t i = 0; i < Rows; ++i)
					mat.m[i][i] = T(1);
				return mat;
			}

			// 行列の乗算（列数一致前提）
			template <size_t OtherCols>
			Matrix<Rows, OtherCols, T> operator*(const Matrix<Cols, OtherCols, T>& other) const noexcept {
				Matrix<Rows, OtherCols, T> result{};
				for (size_t row = 0; row < Rows; ++row)
					for (size_t col = 0; col < OtherCols; ++col)
						for (size_t k = 0; k < Cols; ++k)
							result.m[row][col] += m[row][k] * other.m[k][col];
				return result;
			}

			// 添字アクセス
			std::array<T, Cols>& operator[](size_t row) noexcept { return m[row]; }
			const std::array<T, Cols>& operator[](size_t row) const noexcept { return m[row]; }

			Matrix& operator=(const Matrix& other) noexcept {
				assert(Rows == other.Rows && Cols == other.Cols);
				m = other.m;
				return *this;
			}

			const float* ToPointer() const noexcept { return &m[0][0]; }
		};

		// 平行移動行列
		template<typename T>
		Matrix<4, 4, T> MakeTranslationMatrix(const Vec3<T>& t) noexcept {
			auto m = Matrix<4, 4, T>::Identity();
			m[0][3] = t.x;
			m[1][3] = t.y;
			m[2][3] = t.z;
			return m;
		}

		// スケーリング行列
		template<typename T>
		Matrix<4, 4, T> MakeScalingMatrix(const Vec3<T>& s) noexcept {
			Matrix<4, 4, T> m = {};
			m[0][0] = s.x;
			m[1][1] = s.y;
			m[2][2] = s.z;
			m[3][3] = T(1);
			return m;
		}

		// クォータニオン → 回転行列
		template<typename T>
		Matrix<4, 4, T> MakeRotationMatrix(const Quat<T>& q) noexcept {
			Matrix<4, 4, T> m;
			T x = q.x, y = q.y, z = q.z, w = q.w;
			T xx = x * x, yy = y * y, zz = z * z;
			T xy = x * y, xz = x * z, yz = y * z;
			T wx = w * x, wy = w * y, wz = w * z;

			m[0][0] = T(1) - T(2) * (yy + zz);
			m[0][1] = T(2) * (xy - wz);
			m[0][2] = T(2) * (xz + wy);
			m[0][3] = T(0);

			m[1][0] = T(2) * (xy + wz);
			m[1][1] = T(1) - T(2) * (xx + zz);
			m[1][2] = T(2) * (yz - wx);
			m[1][3] = T(0);

			m[2][0] = T(2) * (xz - wy);
			m[2][1] = T(2) * (yz + wx);
			m[2][2] = T(1) - T(2) * (xx + yy);
			m[2][3] = T(0);

			m[3][0] = T(0);
			m[3][1] = T(0);
			m[3][2] = T(0);
			m[3][3] = T(1);

			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakeLookAtMatrixLH(const Vec3<T>& eye, const Vec3<T>& target, const Vec3<T>& up) noexcept {
			Vec3<T> zaxis = (target - eye).normalized();   // forward
			Vec3<T> xaxis = up.cross(zaxis).normalized();  // right
			Vec3<T> yaxis = zaxis.cross(xaxis);            // up (直交再構成)

			Matrix<4, 4, T> m = {};

			m[0][0] = xaxis.x; m[0][1] = yaxis.x; m[0][2] = zaxis.x; m[0][3] = T(0);
			m[1][0] = xaxis.y; m[1][1] = yaxis.y; m[1][2] = zaxis.y; m[1][3] = T(0);
			m[2][0] = xaxis.z; m[2][1] = yaxis.z; m[2][2] = zaxis.z; m[2][3] = T(0);

			m[3][0] = -xaxis.dot(eye);
			m[3][1] = -yaxis.dot(eye);
			m[3][2] = -zaxis.dot(eye);
			m[3][3] = T(1);

			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakeLookAtMatrixRH(const Vec3<T>& eye, const Vec3<T>& center, const Vec3<T>& up) noexcept {
			Vec3<T> f = (center - eye).normalized();   // forward
			Vec3<T> s = f.cross(up).normalized();      // right
			Vec3<T> u = s.cross(f);                    // new up

			Matrix<4, 4, T> m{};

			m[0][0] = s.x;  m[1][0] = s.y;  m[2][0] = s.z;  m[3][0] = -s.dot(eye);
			m[0][1] = u.x;  m[1][1] = u.y;  m[2][1] = u.z;  m[3][1] = -u.dot(eye);
			m[0][2] = -f.x; m[1][2] = -f.y; m[2][2] = -f.z; m[3][2] = f.dot(eye);
			m[0][3] = 0;    m[1][3] = 0;    m[2][3] = 0;    m[3][3] = 1;

			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakePerspectiveMatrixLH(T fovY, T aspect, T nearZ, T farZ) noexcept {
			T f = T(1) / std::tan(fovY / T(2));

			Matrix<4, 4, T> m = {};

			m[0][0] = f / aspect;
			m[1][1] = f;
			m[2][2] = farZ / (farZ - nearZ);
			m[2][3] = T(1);
			m[3][2] = (-nearZ * farZ) / (farZ - nearZ);
			m[3][3] = T(0);

			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakePerspectiveMatrixRH(T fovY, T aspect, T zNear, T zFar) noexcept {
			T f = T(1) / std::tan(fovY / T(2));

			Matrix<4, 4, T> m{};
			m[0][0] = f / aspect;
			m[1][1] = f;
			m[2][2] = (zFar + zNear) / (zNear - zFar);
			m[2][3] = -1;
			m[3][2] = (2 * zFar * zNear) / (zNear - zFar);
			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakeOrthographicMatrixLH(T left, T right, T bottom, T top, T nearZ, T farZ) noexcept {
			Matrix<4, 4, T> m = {};

			m[0][0] = T(2) / (right - left);
			m[1][1] = T(2) / (top - bottom);
			m[2][2] = T(1) / (farZ - nearZ);
			m[3][0] = -(right + left) / (right - left);
			m[3][1] = -(top + bottom) / (top - bottom);
			m[3][2] = -nearZ / (farZ - nearZ);
			m[3][3] = T(1);

			return m;
		}

		template<typename T>
		Matrix<4, 4, T> MakeOrthographicMatrixRH(T left, T right, T bottom, T top, T zNear, T zFar) noexcept {
			Matrix<4, 4, T> m{};

			m[0][0] = T(2) / (right - left);
			m[1][1] = T(2) / (top - bottom);
			m[2][2] = T(-2) / (zFar - zNear);  // 右手系のためマイナス
			m[3][0] = -(right + left) / (right - left);
			m[3][1] = -(top + bottom) / (top - bottom);
			m[3][2] = -(zFar + zNear) / (zFar - zNear);
			m[3][3] = T(1);

			return m;
		}

		template <size_t Rows, size_t Cols, typename T>
		Matrix<Cols, Rows, T> TransposeMatrix(const Matrix<Rows, Cols, T>& mat) noexcept {
			Matrix<Cols, Rows, T> result;
			for (size_t i = 0; i < Rows; ++i)
				for (size_t j = 0; j < Cols; ++j)
					result[j][i] = mat[i][j];
			return result;
		}

		template<typename T>
		Matrix<4, 4, T> Inverse(const Matrix<4, 4, T>& m) noexcept {
			Matrix<4, 4, T> inv;
			const auto& a = m.m;

			inv[0][0] = a[1][1] * a[2][2] * a[3][3] - a[1][1] * a[2][3] * a[3][2] - a[2][1] * a[1][2] * a[3][3]
				+ a[2][1] * a[1][3] * a[3][2] + a[3][1] * a[1][2] * a[2][3] - a[3][1] * a[1][3] * a[2][2];

			inv[0][1] = -a[0][1] * a[2][2] * a[3][3] + a[0][1] * a[2][3] * a[3][2] + a[2][1] * a[0][2] * a[3][3]
				- a[2][1] * a[0][3] * a[3][2] - a[3][1] * a[0][2] * a[2][3] + a[3][1] * a[0][3] * a[2][2];

			inv[0][2] = a[0][1] * a[1][2] * a[3][3] - a[0][1] * a[1][3] * a[3][2] - a[1][1] * a[0][2] * a[3][3]
				+ a[1][1] * a[0][3] * a[3][2] + a[3][1] * a[0][2] * a[1][3] - a[3][1] * a[0][3] * a[1][2];

			inv[0][3] = -a[0][1] * a[1][2] * a[2][3] + a[0][1] * a[1][3] * a[2][2] + a[1][1] * a[0][2] * a[2][3]
				- a[1][1] * a[0][3] * a[2][2] - a[2][1] * a[0][2] * a[1][3] + a[2][1] * a[0][3] * a[1][2];

			inv[1][0] = -a[1][0] * a[2][2] * a[3][3] + a[1][0] * a[2][3] * a[3][2] + a[2][0] * a[1][2] * a[3][3]
				- a[2][0] * a[1][3] * a[3][2] - a[3][0] * a[1][2] * a[2][3] + a[3][0] * a[1][3] * a[2][2];

			inv[1][1] = a[0][0] * a[2][2] * a[3][3] - a[0][0] * a[2][3] * a[3][2] - a[2][0] * a[0][2] * a[3][3]
				+ a[2][0] * a[0][3] * a[3][2] + a[3][0] * a[0][2] * a[2][3] - a[3][0] * a[0][3] * a[2][2];

			inv[1][2] = -a[0][0] * a[1][2] * a[3][3] + a[0][0] * a[1][3] * a[3][2] + a[1][0] * a[0][2] * a[3][3]
				- a[1][0] * a[0][3] * a[3][2] - a[3][0] * a[0][2] * a[1][3] + a[3][0] * a[0][3] * a[1][2];

			inv[1][3] = a[0][0] * a[1][2] * a[2][3] - a[0][0] * a[1][3] * a[2][2] - a[1][0] * a[0][2] * a[2][3]
				+ a[1][0] * a[0][3] * a[2][2] + a[2][0] * a[0][2] * a[1][3] - a[2][0] * a[0][3] * a[1][2];

			inv[2][0] = a[1][0] * a[2][1] * a[3][3] - a[1][0] * a[2][3] * a[3][1] - a[2][0] * a[1][1] * a[3][3]
				+ a[2][0] * a[1][3] * a[3][1] + a[3][0] * a[1][1] * a[2][3] - a[3][0] * a[1][3] * a[2][1];

			inv[2][1] = -a[0][0] * a[2][1] * a[3][3] + a[0][0] * a[2][3] * a[3][1] + a[2][0] * a[0][1] * a[3][3]
				- a[2][0] * a[0][3] * a[3][1] - a[3][0] * a[0][1] * a[2][3] + a[3][0] * a[0][3] * a[2][1];

			inv[2][2] = a[0][0] * a[1][1] * a[3][3] - a[0][0] * a[1][3] * a[3][1] - a[1][0] * a[0][1] * a[3][3]
				+ a[1][0] * a[0][3] * a[3][1] + a[3][0] * a[0][1] * a[1][3] - a[3][0] * a[0][3] * a[1][1];

			inv[2][3] = -a[0][0] * a[1][1] * a[2][3] + a[0][0] * a[1][3] * a[2][1] + a[1][0] * a[0][1] * a[2][3]
				- a[1][0] * a[0][3] * a[2][1] - a[2][0] * a[0][1] * a[1][3] + a[2][0] * a[0][3] * a[1][1];

			inv[3][0] = -a[1][0] * a[2][1] * a[3][2] + a[1][0] * a[2][2] * a[3][1] + a[2][0] * a[1][1] * a[3][2]
				- a[2][0] * a[1][2] * a[3][1] - a[3][0] * a[1][1] * a[2][2] + a[3][0] * a[1][2] * a[2][1];

			inv[3][1] = a[0][0] * a[2][1] * a[3][2] - a[0][0] * a[2][2] * a[3][1] - a[2][0] * a[0][1] * a[3][2]
				+ a[2][0] * a[0][2] * a[3][1] + a[3][0] * a[0][1] * a[2][2] - a[3][0] * a[0][2] * a[2][1];

			inv[3][2] = -a[0][0] * a[1][1] * a[3][2] + a[0][0] * a[1][2] * a[3][1] + a[1][0] * a[0][1] * a[3][2]
				- a[1][0] * a[0][2] * a[3][1] - a[3][0] * a[0][1] * a[1][2] + a[3][0] * a[0][2] * a[1][1];

			inv[3][3] = a[0][0] * a[1][1] * a[2][2] - a[0][0] * a[1][2] * a[2][1] - a[1][0] * a[0][1] * a[2][2]
				+ a[1][0] * a[0][2] * a[2][1] + a[2][0] * a[0][1] * a[1][2] - a[2][0] * a[0][2] * a[1][1];

			T det = a[0][0] * inv[0][0] + a[0][1] * inv[1][0] + a[0][2] * inv[2][0] + a[0][3] * inv[3][0];
			assert(det != T(0));

			T invDet = T(1) / det;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j)
					inv[i][j] *= invDet;

			return inv;
		}

		using Matrix4x4f = Matrix<4, 4, float>;
		using Matrix4x4d = Matrix<4, 4, double>;
	}
}