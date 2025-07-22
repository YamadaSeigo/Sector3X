/*****************************************************************//**
 * @file   Partition.hpp
 * @brief パーティションのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <vector>

#include "Math/Vector.hpp"
#include "Math/AABB.hpp"

#include "SpatialChunk.h"

namespace SectorFW
{
	/**
	 * @brief チャンクを検索する際のポリシーを定義する列挙型
	 */
	enum class EOutOfBoundsPolicy {
		Reject,
		ClampToEdge
	};
	/**
	 * @brief Partitionが実装する必要のあるインターフェースを定義するコンセプト
	 */
	template <typename Derived>
	concept PartitionConcept = requires(Derived t, Math::Vec3f v, ChunkSizeType size, EOutOfBoundsPolicy policy) {
		Derived{ size,size,size };
		{ t.GetChunk(v, policy) } -> std::same_as< std::optional<SpatialChunk*>>;
		{ t.GetGlobalEntityManager() } -> std::same_as<ECS::EntityManager&>;
	};
}