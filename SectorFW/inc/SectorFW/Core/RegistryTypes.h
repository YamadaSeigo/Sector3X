/*****************************************************************//**
 * @file   RegistryTypes.h
 * @brief レジストリ関連の型を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

namespace SectorFW
{
	// レベルごとに一意なインスタンスID（レベル再ロードのたびに新規値を割り当て）
	using LevelID = std::uint32_t;
	/**
	 * @brief PartitionScheme: 空間分割の方式を定義する列挙型
	 */
	enum class PartitionScheme : uint8_t { Grid2D, Grid3D, Quadtree2D, Octree3D, BVH, SAP };
	/**
	 * @brief SpatialChunkKey: どの SpatialChunk を指すか（Level  区画  世代）
	 */
	struct SpatialChunkKey {
		LevelID     level{};
		PartitionScheme scheme{ PartitionScheme::Grid2D };
		uint8_t       depth{ 0 };          // Grid2Dは常に0、Quadtree/Octreeでレベルを入れる
		std::uint16_t generation{ 0 };
		uint64_t      code{ kInvalidCode };  // Grid2D: Morton2D(x,y) / Quad: Morton2D / Oct: Morton3D

		static constexpr uint64_t kInvalidCode = (std::numeric_limits<uint64_t>::max)();

		bool operator==(const SpatialChunkKey& o) const noexcept = default;
		SpatialChunkKey& operator=(const SpatialChunkKey& mgr) noexcept = default;

		inline bool IsValid() const noexcept {
			return code != SpatialChunkKey::kInvalidCode;
		}
	};
}
