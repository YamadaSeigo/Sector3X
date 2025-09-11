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
#include "Math/Frustum.hpp"

#include "SpatialChunk.h"
#include "EntityManagerRegistryService.h"

#include "Debug/DebugType.h"

namespace SectorFW
{
	/**
	 * @brief チャンクを検索する際のポリシーを定義する列挙型
	 */
	enum class EOutOfBoundsPolicy {
		Reject, // 範囲外のチャンクを拒否
		ClampToEdge, // 範囲外のチャンクをエッジにクランプ
	};
	/**
	 * @brief Partitionが実装する必要のあるインターフェースを定義するコンセプト
	 */
	template <typename Derived>
	concept PartitionConcept = requires(Derived t, Math::Vec3f v, ChunkSizeType size, float chunkSize,
		EOutOfBoundsPolicy policy,
		EntityManagerRegistry & reg, LevelID level,
		const Math::Frustumf & fr, float ymin, float ymax, Math::Vec3f cp, float hy,
		Debug::LineVertex * outLine, uint32_t lineCapacity, uint32_t displayCount
		)
	{
		Derived{ size,size,chunkSize };
		{ t.GetChunk(v, policy) } -> std::same_as<std::optional<SpatialChunk*>>;
		{ t.GetGlobalEntityManager() } -> std::same_as<ECS::EntityManager&>;
		{ t.RegisterAllChunks(reg, level) } -> std::same_as<void>;
		{ t.GetEntityNum() } -> std::same_as<size_t>;
		{ t.CullChunks(fr, ymin, ymax) } -> std::same_as<std::vector<SpatialChunk*>>;
		{ t.CullChunkLine(fr, cp, hy, outLine, lineCapacity, displayCount) } -> std::same_as<uint32_t>;
	};

	template <typename Derived>
	concept HasPartitionUpdate = requires(Derived t, double deltaTime)
	{
		{ t.Update(deltaTime) } -> std::same_as<void>;
	};
}