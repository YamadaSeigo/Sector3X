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
			VecT lb;  // 最小点
			VecT ub;  // 最大点

			AABB() : lb(T(0)), ub(T(0)) {}
			AABB(const VecT& lower_bound_, const VecT& upper_bound_) : lb(lower_bound_), ub(upper_bound_) {}

			// 幅・高さ・奥行き（フルサイズ）
			VecT size() const {
				return ub - lb;
			}

			// 中心
			VecT center() const {
				return (lb + ub) * T(0.5);
			}

			// 半サイズ（half-extent）
			VecT extent() const {
				return (ub - lb) * T(0.5);
			}

			// 点を内包？
			bool contains(const VecT& point) const {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (point[i] < lb[i] || point[i] > ub[i]) return false;
				}
				return true;
			}

			// 交差？
			bool intersects(const AABB& other) const {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (ub[i] < other.lb[i] || lb[i] > other.ub[i]) return false;
				}
				return true;
			}

			// 点で拡張
			void expandToInclude(const VecT& point) {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (point[i] < lb[i]) lb[i] = point[i];
					if (point[i] > ub[i]) ub[i] = point[i];
				}
			}

			// AABBで拡張
			void expandToInclude(const AABB& other) {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					if (other.lb[i] < lb[i]) lb[i] = other.lb[i];
					if (other.ub[i] > ub[i]) ub[i] = other.ub[i];
				}
			}

			// 2つのAABBの和（外接最小AABB）を返すユーティリティ
			template<typename T, typename VecT>
			static AABB<T, VecT> Union(const AABB<T, VecT>& a, const AABB<T, VecT>& b) {
				AABB<T, VecT> out;
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					out.lb[i] = (std::min)(a.lb[i], b.lb[i]);
					out.ub[i] = (std::max)(a.ub[i], b.ub[i]);
				}
				return out;
			}
		};

		using AABB2f = AABB<float, Vec2f>;
		using AABB3f = AABB<float, Vec3f>;		
	}
}