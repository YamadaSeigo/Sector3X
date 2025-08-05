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
			static Matrix Identity() {
				static_assert(Rows == Cols, "Identity matrix must be square.");
				Matrix mat;
				for (size_t i = 0; i < Rows; ++i)
					mat.m[i][i] = T(1);
				return mat;
			}

			// 行列の乗算（列数一致前提）
			template <size_t OtherCols>
			Matrix<Rows, OtherCols, T> operator*(const Matrix<Cols, OtherCols, T>& other) const {
				Matrix<Rows, OtherCols, T> result{};
				for (size_t row = 0; row < Rows; ++row)
					for (size_t col = 0; col < OtherCols; ++col)
						for (size_t k = 0; k < Cols; ++k)
							result.m[row][col] += m[row][k] * other.m[k][col];
				return result;
			}

			// 添字アクセス
			std::array<T, Cols>& operator[](size_t row) { return m[row]; }
			const std::array<T, Cols>& operator[](size_t row) const { return m[row]; }

			Matrix& operator=(const Matrix& other) {
				assert(Rows == other.Rows && Cols == other.Cols);
				m = other.m;
				return *this;
			}

			const float* ToPointer() const { return &m[0][0]; }
		};

		// 平行移動行列
		template<typename T>
		Matrix<4, 4, T> MakeTranslationMatrix(const Vec3<T>& t) {
			auto m = Matrix<4, 4, T>::Identity();
			m[0][3] = t.x;
			m[1][3] = t.y;
			m[2][3] = t.z;
			return m;
		}

		// スケーリング行列
		template<typename T>
		Matrix<4, 4, T> MakeScalingMatrix(const Vec3<T>& s) {
			Matrix<4, 4, T> m = {};
			m[0][0] = s.x;
			m[1][1] = s.y;
			m[2][2] = s.z;
			m[3][3] = T(1);
			return m;
		}

		// クォータニオン → 回転行列
		template<typename T>
		Matrix<4, 4, T> MakeRotationMatrix(const Quat<T>& q) {
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

		using Matrix4x4f = Matrix<4, 4, float>;
		using Matrix4x4d = Matrix<4, 4, double>;
	}
}