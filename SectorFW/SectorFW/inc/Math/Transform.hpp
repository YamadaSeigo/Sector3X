/*****************************************************************//**
 * @file   Transform.h
 * @brief Transform構造体を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

#include "Util/Flatten.hpp"

#include "Core/ECS/component.hpp"

namespace SectorFW
{
	struct TransformSoA; // 前方宣言

	struct Transform
	{
		Math::Vec3f location;
		Math::Quatf rotation;
		Math::Vec3f scale;

		// コンストラクタ
		Transform() : location(0, 0, 0), rotation(0, 0, 0, 1), scale(1, 1, 1) {}
		explicit Transform(const Math::Vec3f& location_, const Math::Quatf& rotation_, const Math::Vec3f& scale_)
			: location(location_), rotation(rotation_), scale(scale_) {
		}
		explicit Transform(float px, float py, float pz,
			float qx, float qy, float qz, float qw,
			float sx, float sy, float sz)
			: location(px, py, pz), rotation(qx, qy, qz, qw), scale(sx, sy, sz) {
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
		TransformSoA() : px(0), py(0), pz(0), qx(0), qy(0), qz(0), qw(1), sx(1), sy(1), sz(1) {}
		explicit TransformSoA(float px_, float py_, float pz_,
			float qx_, float qy_, float qz_, float qw_,
			float sx_, float sy_, float sz_)
			: px(px_), py(py_), pz(pz_), qx(qx_), qy(qy_), qz(qz_), qw(qw_),
			sx(sx_), sy(sy_), sz(sz_) {
		}

		explicit TransformSoA(const Math::Vec3f& location,
			const Math::Quatf& rotation,
			const Math::Vec3f& scale)
			: px(location.x), py(location.y), pz(location.z),
			qx(rotation.x), qy(rotation.y), qz(rotation.z), qw(rotation.w),
			sx(scale.x), sy(scale.y), sz(scale.z) {
		}

		explicit TransformSoA(const Transform& transform)
			: px(transform.location.x), py(transform.location.y), pz(transform.location.z),
			qx(transform.rotation.x), qy(transform.rotation.y), qz(transform.rotation.z), qw(transform.rotation.w),
			sx(transform.scale.x), sy(transform.scale.y), sz(transform.scale.z) {
		}

		// TransformSoAからTransformへの変換
		Transform ToAoS() const {
			return Transform(px, py, pz,
				qx, qy, qz, qw,
				sx, sy, sz);
		}

		DEFINE_SOA(TransformSoA, px, py, pz, qx, qy, qz, qw, sx, sy, sz)
	};

	using CTransform = TransformSoA;
}