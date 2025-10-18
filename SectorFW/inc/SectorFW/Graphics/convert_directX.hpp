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
		template<> inline DirectX::XMMATRIX
			Convert<DirectX::XMMATRIX, Matrix4x4f>(const Matrix4x4f& m) {
			using namespace DirectX;
			return XMMATRIX(
				XMVectorSet(m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3]),
				XMVectorSet(m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3]),
				XMVectorSet(m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]),
				XMVectorSet(m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3])
			);
		}
		/**
		 * @brief XMMATRIX → Matrix4x4f
		 * @param mat 変換するXMMATRIX
		 * @return Matrix4x4f 変換後のMatrix4x4f
		 */
		template<> inline Matrix4x4f
			Convert<Matrix4x4f, DirectX::XMMATRIX>(const DirectX::XMMATRIX& mat) {
			using namespace DirectX;
			XMFLOAT4 r0, r1, r2, r3;
			XMStoreFloat4(&r0, mat.r[0]);
			XMStoreFloat4(&r1, mat.r[1]);
			XMStoreFloat4(&r2, mat.r[2]);
			XMStoreFloat4(&r3, mat.r[3]);

			Matrix4x4f out{};
			out.m[0][0] = r0.x; out.m[0][1] = r0.y; out.m[0][2] = r0.z; out.m[0][3] = r0.w;
			out.m[1][0] = r1.x; out.m[1][1] = r1.y; out.m[1][2] = r1.z; out.m[1][3] = r1.w;
			out.m[2][0] = r2.x; out.m[2][1] = r2.y; out.m[2][2] = r2.z; out.m[2][3] = r2.w;
			out.m[3][0] = r3.x; out.m[3][1] = r3.y; out.m[3][2] = r3.z; out.m[3][3] = r3.w;
			return out;
		}
	}
}