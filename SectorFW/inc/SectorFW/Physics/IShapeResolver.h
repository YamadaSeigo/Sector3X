/*****************************************************************//**
 * @file   IShapeResolver.h
 * @brief 物理シェイプの解決インターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "PhysicsTypes.h"

#include <../third_party/Jolt/Jolt.h>
#include <../third_party/Jolt/Physics/Collision/Shape/Shape.h>

namespace SectorFW
{
	namespace Physics
	{
		/**
		 * @brief IShapeResolver.h 物理シェイプの解決インターフェース
		 */
		struct IShapeResolver {
			virtual ~IShapeResolver() = default;
			virtual JPH::RefConst<JPH::Shape> Resolve(ShapeHandle h) const = 0;
		};
	}
}
