#pragma once

#include <functional>
#include <bitset>

namespace SectorFW
{
	namespace ECS
	{
		constexpr size_t MaxComponents = 64;

		using ComponentTypeID = uint32_t;
		using ComponentMask = std::bitset<MaxComponents>;

		struct ComponentMeta {
			size_t size;
			size_t align;
			bool isSparse;
		};

		//----------------------------------------------
		// Sparse Marker and Macro System
		//----------------------------------------------
		struct SparseComponentTag {};

#define SPARSE_TAG using sparse_tag = SparseComponentTag;
	}
}
