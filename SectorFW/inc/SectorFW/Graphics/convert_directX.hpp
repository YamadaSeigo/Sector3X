/*****************************************************************//**
 * @file   convert_directX.hpp
 * @brief DirectXMathとSectorFWの数学ライブラリ間の変換を提供するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <DirectXMath.h>
#include "Math/convert.hpp"
#include "Math/Matrix.hpp"

namespace SectorFW
{
	namespace Math
	{
		/**
		 * @brief Vector2 → XMFLOAT2
		 * @param v 変換するVec2f
		 * @return XMFLOAT2 変換後のXMFLOAT2
		 */
		template<>
		inline DirectX::XMFLOAT2 Convert<DirectX::XMFLOAT2, Vec2f>(const Vec2f& v) {
			return BitCast<DirectX::XMFLOAT2>(v);
		}
		/**
		 * @brief XMFLOAT2 → Vec2f
		 * @param v 変換するXMFLOAT2
		 * @return Vec2f 変換後のVec2f
		 */
		template<>
		inline Vec2f Convert<Vec2f, DirectX::XMFLOAT2>(const DirectX::XMFLOAT2& v) {
			return BitCast<Vec2f>(v);
		}

		/**
		 * @brief Vec3f → XMVECTOR
		 * @param v 変換するVec3f
		 * @return XMVECTOR 変換後のXMVECTOR
		 */
		template<>
		inline DirectX::XMVECTOR Convert<DirectX::XMVECTOR, Vec3f>(const Vec3f& v) {
			return DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f);
		}
		/**
		 * @brief XMVECTOR → Vec3f
		 * @param v 変換するXMVECTOR
		 * @return Vec3f 変換後のVec3f
		 */
		template<>
		inline Vec3f Convert<Vec3f, DirectX::XMVECTOR>(const DirectX::XMVECTOR& v) {
			DirectX::XMFLOAT3 temp;
			DirectX::XMStoreFloat3(&temp, v);
			return Vec3f{ temp.x, temp.y, temp.z };
		}

		/**
		 * @brief Quatf → XMVECTOR
		 * @param q 変換するQuatf
		 * @return XMVECTOR 変換後のXMVECTOR
		 */
		template<>
		inline DirectX::XMVECTOR Convert<DirectX::XMVECTOR, Quatf>(const Quatf& q) {
			return DirectX::XMVectorSet(q.x, q.y, q.z, q.w);
		}

		template<>
		inline Quatf Convert<Quatf, DirectX::XMVECTOR>(const DirectX::XMVECTOR& v) {
			DirectX::XMFLOAT4 temp;
			DirectX::XMStoreFloat4(&temp, v);
			return Quatf{ temp.x, temp.y, temp.z, temp.w };
		}
		/**
		 * @brief Matrix4x4f → XMMATRIX
		 * @param mat 変換するMatrix4x4f
		 * @return XMMATRIX 変換後のXMMATRIX
		 */
		template<>
		inline DirectX::XMMATRIX Convert<DirectX::XMMATRIX, Matrix4x4f>(const Matrix4x4f& mat) {
			using namespace DirectX;

			return XMMATRIX(
				XMVectorSet(mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0]),
				XMVectorSet(mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1]),
				XMVectorSet(mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2]),
				XMVectorSet(mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3])
			);
		}
		/**
		 * @brief XMMATRIX → Matrix4x4f
		 * @param mat 変換するXMMATRIX
		 * @return Matrix4x4f 変換後のMatrix4x4f
		 */
		template<>
		inline Matrix4x4f Convert<Matrix4x4f, DirectX::XMMATRIX>(const DirectX::XMMATRIX& mat) {
			using namespace DirectX;

			XMFLOAT4 col0, col1, col2, col3;
			XMStoreFloat4(&col0, mat.r[0]); // column 0
			XMStoreFloat4(&col1, mat.r[1]); // column 1
			XMStoreFloat4(&col2, mat.r[2]); // column 2
			XMStoreFloat4(&col3, mat.r[3]); // column 3

			Matrix4x4f result;
			result.m[0][0] = col0.x; result.m[0][1] = col1.x; result.m[0][2] = col2.x; result.m[0][3] = col3.x;
			result.m[1][0] = col0.y; result.m[1][1] = col1.y; result.m[1][2] = col2.y; result.m[1][3] = col3.y;
			result.m[2][0] = col0.z; result.m[2][1] = col1.z; result.m[2][2] = col2.z; result.m[2][3] = col3.z;
			result.m[3][0] = col0.w; result.m[3][1] = col1.w; result.m[3][2] = col2.w; result.m[3][3] = col3.w;

			return result;
		}
	}
}