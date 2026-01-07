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
#include "RegistryTypes.h"
#include "SpatialChunkRegistryService.h"

#include "Debug/DebugType.h"

namespace SFW
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
	concept PartitionConcept = requires(
		Derived t, 
		Math::Vec3f v, ChunkSizeType size, float chunkSize,
		EOutOfBoundsPolicy policy,
		SpatialChunkRegistry & reg, LevelID level,
		const Math::Frustumf & fr,
		const Math::Vec3f & center, float radius,
		Math::Vec3f cp, float hy,
		Debug::LineVertex * outLine, uint32_t lineCapacity, uint32_t displayCount
		)
	{
		//コンストラクタ
		Derived{ size,size,chunkSize };
		//ポイントからチャンクを取得
		{ t.GetChunk(v, reg, level, policy) } -> std::same_as<std::optional<SpatialChunk*>>;
		//分割に依存しないグローバルなエンティティマネージャーを取得
		{ t.GetGlobalEntityManager() } -> std::same_as<ECS::EntityManager&>;
		//チャンクの登録
		{ t.RegisterAllChunks(reg, level) } -> std::same_as<void>;
		//エンティティの総数を取得
		{ t.GetEntityNum() } -> std::same_as<size_t>;
		//フラスタムカリング
		{ t.CullChunks(fr) } -> std::same_as<std::vector<SpatialChunk*>>;
		//半径カリング
		{ t.CullChunks(center, radius) } -> std::same_as<std::vector<SpatialChunk*>>;
		//フラスタムカリング（近い順番）
		{ t.CullChunksNear(fr, cp) } -> std::same_as<std::vector<SpatialChunk*>>;
		//チャンクのワイヤーフレームを取得
		{ t.CullChunkLine(fr, cp, hy, outLine, lineCapacity, displayCount) } -> std::same_as<uint32_t>;
		//チャンクをクリアする
		{ t.CleanChunk() }->std::same_as<void>;
	};
	/**
	 * @brief 更新可能なパーティションを識別するコンセプト
	 */
	template <typename Derived>
	concept HasPartitionUpdate = requires(Derived t, double deltaTime)
	{
		{ t.Update(deltaTime) } -> std::same_as<void>;
	};
}