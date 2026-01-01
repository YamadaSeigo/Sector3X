/*****************************************************************//**
 * @file   DebugType.h
 * @brief デバッグ用の構造体を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../Math/AABB.hpp"
#include "../Math/Frustum.hpp"

namespace SFW
{
	namespace Debug {
		/**
		 * @brief ライン描画用の頂点構造体
		 */
		struct LineVertex {
			Math::Vec3f pos;
			uint32_t rgba = 0xFFFFFFFF;
		};
		/**
		 * @brief デバッグ用の頂点構造体（位置、法線、UV）
		 */
		struct VertexPNUV {
			Math::Vec3f pos;
			Math::Vec3f normal;
			Math::Vec2f uv;
		};

		/// AABB3f を 12 本のライン用 24 頂点で返す（重複あり・インデックス不要）
		inline std::array<Debug::LineVertex, 24> MakeAABBLineVertices(const Math::AABB3f& box, uint32_t rgba = 0xFFFFFFFF)
		{
			using namespace Math;

			const auto& lb = box.lb; // (minX, minY, minZ)
			const auto& ub = box.ub; // (maxX, maxY, maxZ)

			// 8 コーナー（XYZ = 0/1 組み合わせ）
			const Vec3f p000{ lb.x, lb.y, lb.z };
			const Vec3f p100{ ub.x, lb.y, lb.z };
			const Vec3f p110{ ub.x, ub.y, lb.z };
			const Vec3f p010{ lb.x, ub.y, lb.z };

			const Vec3f p001{ lb.x, lb.y, ub.z };
			const Vec3f p101{ ub.x, lb.y, ub.z };
			const Vec3f p111{ ub.x, ub.y, ub.z };
			const Vec3f p011{ lb.x, ub.y, ub.z };

			// 12 エッジを (from, to) の順に並べる
			std::array<Vec3f, 24> pts = {
				// 底面 (Z = min)
				p000, p100,
				p100, p110,
				p110, p010,
				p010, p000,
				// 上面 (Z = max)
				p001, p101,
				p101, p111,
				p111, p011,
				p011, p001,
				// 垂直 4 本
				p000, p001,
				p100, p101,
				p110, p111,
				p010, p011
			};

			std::array<Debug::LineVertex, 24> out{};
			for (size_t i = 0; i < out.size(); ++i) {
				out[i].pos = pts[i];
				out[i].rgba = rgba;
			}
			return out;
		}

		/// AABB3f を 8 頂点 + 24 インデックスで追加（頂点の重複を避けたい場合）
		inline void AppendAABBLineListIndexed(std::vector<Debug::LineVertex>& outVerts,
			std::vector<uint32_t>& outIndices,
			const Math::AABB3f& box,
			uint32_t rgba = 0xFFFFFFFF)
		{
			using namespace Math;

			const auto base = static_cast<uint32_t>(outVerts.size());

			const auto& lb = box.lb;
			const auto& ub = box.ub;

			// 頂点 8 個（固定順）
			const Vec3f corners[8] = {
				{ lb.x, lb.y, lb.z }, // 0: p000
				{ ub.x, lb.y, lb.z }, // 1: p100
				{ ub.x, ub.y, lb.z }, // 2: p110
				{ lb.x, ub.y, lb.z }, // 3: p010
				{ lb.x, lb.y, ub.z }, // 4: p001
				{ ub.x, lb.y, ub.z }, // 5: p101
				{ ub.x, ub.y, ub.z }, // 6: p111
				{ lb.x, ub.y, ub.z }  // 7: p011
			};

			outVerts.reserve(outVerts.size() + 8);
			for (int i = 0; i < 8; ++i) {
				outVerts.push_back(Debug::LineVertex{ corners[i], rgba });
			}

			// ラインリスト用インデックス（12 エッジ × 2 = 24）
			// 底面: 0-1, 1-2, 2-3, 3-0
			// 上面: 4-5, 5-6, 6-7, 7-4
			// 垂直: 0-4, 1-5, 2-6, 3-7
			const uint32_t idx[] = {
				0,1,  1,2,  2,3,  3,0,
				4,5,  5,6,  6,7,  7,4,
				0,4,  1,5,  2,6,  3,7
			};
			outIndices.reserve(outIndices.size() + std::size(idx));
			for (uint32_t i : idx) outIndices.push_back(base + i);
		}

		inline std::array<Debug::LineVertex, 24> MakeFrustumLineVertices(const Math::Frustumf& frustum, uint32_t rgba = 0xFFFFFFFF)
		{
			using namespace Math;

			const auto& Left = frustum.p[(int)FrustumPlane::Left];
			const auto& Right = frustum.p[(int)FrustumPlane::Right];
			const auto& Top = frustum.p[(int)FrustumPlane::Top];
			const auto& Bottom = frustum.p[(int)FrustumPlane::Bottom];
			const auto& Near = frustum.p[(int)FrustumPlane::Near];
			const auto& Far = frustum.p[(int)FrustumPlane::Far];

			// 8 コーナー取得
			Vec3f ntl, ntr, nbl, nbr;
			Vec3f ftl, ftr, fbl, fbr;

			Planef::Intersect3Planes(Near, Left, Top, ntl);
			Planef::Intersect3Planes(Near, Right, Top, ntr);
			Planef::Intersect3Planes(Near, Left, Bottom, nbl);
			Planef::Intersect3Planes(Near, Right, Bottom, nbr);
			Planef::Intersect3Planes(Far, Left, Top, ftl);
			Planef::Intersect3Planes(Far, Right, Top, ftr);
			Planef::Intersect3Planes(Far, Left, Bottom, fbl);
			Planef::Intersect3Planes(Far, Right, Bottom, fbr);

			// 12 エッジを (from, to) の順に並べる
			std::array<Vec3f, 24> pts = {
				// 近クリップ面
				ntl, ntr,
				ntr, nbr,
				nbr, nbl,
				nbl, ntl,
				// 遠クリップ面
				ftl, ftr,
				ftr, fbr,
				fbr, fbl,
				fbl, ftl,
				// 接続エッジ 4 本
				ntl, ftl,
				ntr, ftr,
				nbr, fbr,
				nbl, fbl
			};
			std::array<Debug::LineVertex, 24> out{};
			for (size_t i = 0; i < out.size(); ++i) {
				out[i].pos = pts[i];
				out[i].rgba = rgba;
			}
			return out;
		}
	}
}
