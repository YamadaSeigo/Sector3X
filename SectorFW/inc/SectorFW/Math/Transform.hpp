/*****************************************************************//**
 * @file   Transform.h
 * @brief Transform構造体を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include "Vector.hpp"
#include "Quaternion.hpp"
#include "Matrix.hpp"

#include "../Util/Flatten.hpp"

#include "../Core/ECS/component.hpp"

namespace SFW
{
	struct TransformSoA; // 前方宣言

	struct Transform
	{
		Math::Vec3f location;
		Math::Quatf rotation;
		Math::Vec3f scale;

		// コンストラクタ
		Transform() noexcept : location(0, 0, 0), rotation(0, 0, 0, 1), scale(1, 1, 1) {}
		explicit Transform(const Math::Vec3f& location_, const Math::Quatf& rotation_, const Math::Vec3f& scale_) noexcept
			: location(location_), rotation(rotation_), scale(scale_) {
		}
		explicit Transform(float px, float py, float pz,
			float qx, float qy, float qz, float qw,
			float sx, float sy, float sz) noexcept
			: location(px, py, pz), rotation(qx, qy, qz, qw), scale(sx, sy, sz) {
		}

		Math::Matrix4x4f ToMatrix() const noexcept {
			
			//行列の規約は右かけで行優先、なので平行成分は[0][3],[1][3],[2][3]に入れる

			Math::Matrix4x4f mat = Math::Matrix4x4f::Identity();

			// クォータニオンを回転行列に変換
			const float xx = rotation.x * rotation.x;
			const float yy = rotation.y * rotation.y;
			const float zz = rotation.z * rotation.z;

			// 回転行列とスケーリングを組み合わせる
			mat[0][0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
			mat[0][1] = (2.0f * (rotation.x * rotation.y + rotation.z * rotation.w)) * scale.x;
			mat[0][2] = (2.0f * (rotation.x * rotation.z - rotation.y * rotation.w)) * scale.x;

			mat[1][0] = (2.0f * (rotation.x * rotation.y - rotation.z * rotation.w)) * scale.y;
			mat[1][1] = (1.0f - 2.0f * (xx + zz)) * scale.y;
			mat[1][2] = (2.0f * (rotation.y * rotation.z + rotation.x * rotation.w)) * scale.y;

			mat[2][0] = (2.0f * (rotation.x * rotation.z + rotation.y * rotation.w)) * scale.z;
			mat[2][1] = (2.0f * (rotation.y * rotation.z - rotation.x * rotation.w)) * scale.z;
			mat[2][2] = (1.0f - 2.0f * (xx + yy)) * scale.z;

			// 平行移動を設定
			mat[0][3] = location.x;
			mat[1][3] = location.y;
			mat[2][3] = location.z;

			mat[3][0] = 0.0f;
			mat[3][1] = 0.0f;
			mat[3][2] = 0.0f;
			mat[3][3] = 1.0f;

			return mat;
		}
	};

	struct TransformSoA
	{
		union
		{
			struct {
				float px, py, pz; // 位置
				float qx, qy, qz, qw; // 回転（クォータニオン）
				float sx, sy, sz; // スケール
			};
			struct {
				Math::Vec3f location; // 位置
				Math::Quatf rotation; // 回転（クォータニオン）
				Math::Vec3f scale; // スケール
			};
			float data[10]; // データを一括で扱うための配列
		};

		// コンストラクタ
		TransformSoA() noexcept : px(0), py(0), pz(0), qx(0), qy(0), qz(0), qw(1), sx(1), sy(1), sz(1) {}
		explicit TransformSoA(float px_, float py_, float pz_,
			float qx_, float qy_, float qz_, float qw_,
			float sx_, float sy_, float sz_) noexcept
			: px(px_), py(py_), pz(pz_), qx(qx_), qy(qy_), qz(qz_), qw(qw_),
			sx(sx_), sy(sy_), sz(sz_) {
		}

		explicit TransformSoA(const Math::Vec3f& location,
			const Math::Quatf& rotation,
			const Math::Vec3f& scale) noexcept
			: px(location.x), py(location.y), pz(location.z),
			qx(rotation.x), qy(rotation.y), qz(rotation.z), qw(rotation.w),
			sx(scale.x), sy(scale.y), sz(scale.z) {
		}

		explicit TransformSoA(const Transform& transform) noexcept
			: px(transform.location.x), py(transform.location.y), pz(transform.location.z),
			qx(transform.rotation.x), qy(transform.rotation.y), qz(transform.rotation.z), qw(transform.rotation.w),
			sx(transform.scale.x), sy(transform.scale.y), sz(transform.scale.z) {
		}

		// TransformSoAからTransformへの変換
		Transform ToAoS() const noexcept {
			return Transform(px, py, pz,
				qx, qy, qz, qw,
				sx, sy, sz);
		}

		TransformSoA& operator=(const TransformSoA& transform) noexcept = default;

		DEFINE_SOA(TransformSoA, px, py, pz, qx, qy, qz, qw, sx, sy, sz)
	};

	using CTransform = TransformSoA;
}
