/*****************************************************************//**
 * @file   AABB.hpp
 * @brief Axis-Aligned Bounding Box (AABB)を定義するクラス
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <limits>
#include "Vector.hpp" // Vec<T, N> を定義しているヘッダー

namespace SFW
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

			// AABBを無効化
			void invalidate() {
				for (size_t i = 0; i < sizeof(VecT) / sizeof(T); ++i) {
					lb[i] = (std::numeric_limits<T>::max)();
					ub[i] = std::numeric_limits<T>::lowest();
				}
			}
		};

		using AABB2f = AABB<float, Vec2f>;
		using AABB3f = AABB<float, Vec3f>;

		// ---------------------------------------------------------
		// AABB 同士の交差
		// ---------------------------------------------------------
		template<typename T>
		static AABB<T, Vec3<T>> IntersectAABB(const AABB<T, Vec3<T>>& a, const AABB<T, Vec3<T>>& b)
		{
			Math::AABB3f r;
			r.lb.x = (std::max)(a.lb.x, b.lb.x);
			r.lb.y = (std::max)(a.lb.y, b.lb.y);
			r.lb.z = (std::max)(a.lb.z, b.lb.z);

			r.ub.x = (std::min)(a.ub.x, b.ub.x);
			r.ub.y = (std::min)(a.ub.y, b.ub.y);
			r.ub.z = (std::min)(a.ub.z, b.ub.z);

			if (r.lb.x > r.ub.x || r.lb.y > r.ub.y || r.lb.z > r.ub.z)
			{
				// 交差なし → 適当に a を返すか、空AABBにする
				return a;
			}
			return r;
		}

		template<typename T>
		static inline void ExpandAABB(AABB<T, Vec3<T>>& b, const Vec3<T>& p) {
			b.lb.x = (std::min)(b.lb.x, p.x);
			b.lb.y = (std::min)(b.lb.y, p.y);
			b.lb.z = (std::min)(b.lb.z, p.z);
			b.ub.x = (std::max)(b.ub.x, p.x);
			b.ub.y = (std::max)(b.ub.y, p.y);
			b.ub.z = (std::max)(b.ub.z, p.z);
		}
	}
}