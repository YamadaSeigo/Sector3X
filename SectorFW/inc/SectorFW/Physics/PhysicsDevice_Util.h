/*****************************************************************//**
 * @file   PhysicsDevice_Util.h
 * @brief PhysicsDevice 用のユーティリティ
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include "PhysicsTypes.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>
#include <limits>

namespace SectorFW
{
	namespace Physics
	{
		// Jolt が double precision の場合は R* 型を使う
#if defined(JPH_DOUBLE_PRECISION) && JPH_DOUBLE_PRECISION
		using JMat = JPH::RMat44;
		using JVec3 = JPH::RVec3;
#else
		using JMat = JPH::Mat44;
		using JVec3 = JPH::Vec3;
#endif

		inline JPH::Quat ToJQuat(const Quatf& q) noexcept { return JPH::Quat(q.x, q.y, q.z, q.w); }
		inline JVec3     ToJVec3(const Vec3f& v) noexcept { return JVec3(v.x, v.y, v.z); }
		inline JMat      ToJMatRT(const Mat34f& tm) {
			// 回転＋平行移動の 3x4 → 4x4（Joltは列優先・右手前提、RTでOK）
			return JMat::sRotationTranslation(ToJQuat(tm.rot), ToJVec3(tm.pos));
		}

		inline Vec3f FromJVec3(const JVec3& v) noexcept { return Vec3f{ (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() }; }
		inline Quatf FromJQuat(const JPH::Quat& q) noexcept { return Quatf{ q.GetX(), q.GetY(), q.GetZ(), q.GetW() }; }

		// BodyID を unordered_map のキーにするためのハッシュ
		struct BodyIDHash {
			size_t operator()(const JPH::BodyID& id) const noexcept {
				return std::hash<uint32_t>{}(id.GetIndexAndSequenceNumber());
			}
		};
		struct BodyIDEq {
			bool operator()(const JPH::BodyID& a, const JPH::BodyID& b) const noexcept {
				return a.GetIndexAndSequenceNumber() == b.GetIndexAndSequenceNumber();
			}
		};

		// ===== Pending 判定／生成（センチネルは 0xFFFFFFFF） =====
		inline bool IsPendingBodyID(const JPH::BodyID& id) noexcept {
			return id.GetIndexAndSequenceNumber() == (std::numeric_limits<uint32_t>::max)();
		}
		inline JPH::BodyID PendingBodyID() noexcept {
			return JPH::BodyID((std::numeric_limits<uint32_t>::max)());
		}
	}
}
