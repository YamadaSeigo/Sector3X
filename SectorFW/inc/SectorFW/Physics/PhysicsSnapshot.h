/*****************************************************************//**
 * @file   PhysicsSnapshot.h
 * @brief 物理エンジンのスナップショットデータを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <vector>
#include <cstdint>

#include "Core/ECS/entity.h"
#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

namespace SectorFW
{
	namespace Physics
	{
		/**
		 * @brief エンティティのポーズを表す構造体
		 */
		struct Pose {
			ECS::EntityID e;
			Math::Vec3f  pos;
			Math::Quatf  rot;
		};
		/**
		 * @brief 接触イベントを表す構造体
		 */
		struct ContactEvent {
			enum Type { Begin, Persist, End } type;
			ECS::EntityID a, b;
			Math::Vec3f  pointWorld;
			Math::Vec3f  normalWorld;
			float  impulse;
		};
		/**
		 * @brief レイキャストヒットイベントを表す構造体
		 */
		struct RayCastHitEvent {
			uint32_t requestId;
			bool     hit;
			ECS::EntityID   hitEntity;
			Math::Vec3f    position;
			Math::Vec3f    normal;
			float    distance;
		};
		/**
		 * @brief 物理エンジンのスナップショットデータを表す構造体
		 */
		struct PhysicsSnapshot {
			std::vector<Pose>          poses;       // curr (前フレームは別に保持して補間)
			std::vector<ContactEvent>  contacts;
			std::vector<RayCastHitEvent> rayHits;
		};
	}
}
