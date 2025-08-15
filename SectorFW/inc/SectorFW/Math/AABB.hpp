/*****************************************************************//**
 * @file   AABB.hpp
 * @brief Axis-Aligned Bounding Box (AABB)を定義するクラス
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <limits>
#include "Vector.hpp" // Vec<T, N> を定義しているヘッダー

namespace SectorFW
{
	namespace Math
	{
		template<typename T, typename VecT>
		struct AABB {
			VecT lower_bound;  // 最小点（左下 / 前）
			VecT upper_bound;  // 最大点（右上 / 奥）

			AABB() : lower_bound(T(0)), upper_bound(T(0)) {}
			AABB(const VecT& lower_bound_, const VecT& upper_bound_) : lower_bound(lower_bound_), upper_bound(upper_bound_) {}

			// サイズ（幅や高さ）
			VecT size() const {
				return upper_bound - lower_bound;
			}

			// 中心
			VecT center() const {
				return (lower_bound + upper_bound) * T(0.5);
			}

			// 点がこのAABB内に含まれているか
			bool contains(const VecT& point) const {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (point[i] < lower_bound[i] || point[i] > upper_bound[i])
						return false;
				}
				return true;
			}

			// AABBが別のAABBと交差しているか
			bool intersects(const AABB& other) const {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (upper_bound[i] < other.lower_bound[i] || lower_bound[i] > other.upper_bound[i])
						return false;
				}
				return true;
			}

			// AABBを拡張して点を包むようにする
			void expandToInclude(const VecT& point) {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (point[i] < lower_bound[i]) lower_bound[i] = point[i];
					if (point[i] > upper_bound[i]) upper_bound[i] = point[i];
				}
			}
		};

		using AABB2f = AABB<float, Vec2f>;
		using AABB3f = AABB<float, Vec3f>;
	}
}