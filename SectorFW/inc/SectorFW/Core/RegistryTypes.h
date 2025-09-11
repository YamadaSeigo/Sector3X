#pragma once

namespace SectorFW
{
	// レベルごとに一意なインスタンスID（レベル再ロードのたびに新規値を割り当て）
	using LevelID = std::uint32_t;

	enum class PartitionScheme : uint8_t { Grid2D, Grid3D, Quadtree2D, Octree3D, BVH, SAP };

	// ManagerKey: どの EntityManager を指すか（Level  区画  世代）
	struct EntityManagerKey {
		LevelID     level{};
		PartitionScheme scheme{ PartitionScheme::Grid2D };
		uint8_t       depth{ 0 };          // Grid2Dは常に0、Quadtree/Octreeでレベルを入れる
		std::uint16_t generation{ 0 };
		uint64_t      code{ 0 };          // Grid2D: Morton2D(x,y) / Quad: Morton2D / Oct: Morton3D
		bool operator==(const EntityManagerKey& o) const = default;
	};
}
