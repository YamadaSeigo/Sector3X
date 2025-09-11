#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include "Vector.hpp"   // SectorFW::Math::Vec3f
#include "AABB.hpp"     // SectorFW::Math::AABB2f / AABB3f (lb, ub)
#include "Matrix.hpp"   // 行列からの抽出に使う場合（必要なら）

namespace SectorFW::Math {
	// n・x + d = 0
	struct Planef {
		Vec3f n{ 0.f, 0.f, 1.f };
		float d{ 0.f };

		Planef() = default;
		constexpr Planef(const Vec3f& normal, float dd) : n(normal), d(dd) {}

		// ax + by + cz + d = 0 から（正規化オプション）
		static Planef FromCoefficients(float a, float b, float c, float d, bool normalize = true) noexcept {
			Planef pl{ {a,b,c}, d };
			if (normalize) pl.Normalize();
			return pl;
		}

		// 点＋法線から
		static Planef FromPointNormal(const Vec3f& point, const Vec3f& normal, bool normalize = true) noexcept {
			Vec3f nn = normal;
			if (normalize) {
				const float len = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
				if (len > 0.f) {
					const float inv = 1.f / len;
					nn.x *= inv; nn.y *= inv; nn.z *= inv;
				}
			}
			return { nn, -(nn.x * point.x + nn.y * point.y + nn.z * point.z) };
		}

		// 片側正規化
		void Normalize() noexcept {
			const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			if (len > 0.f) {
				const float inv = 1.f / len;
				n.x *= inv; n.y *= inv; n.z *= inv; d *= inv;
			}
		}
		Planef Normalized() const noexcept { Planef t = *this; t.Normalize(); return t; }

		// 符号付き距離（n が非正規化でもそのスケールの距離）
		float SignedDistance(const Vec3f& p) const noexcept {
			return n.x * p.x + n.y * p.y + n.z * p.z + d;
		}
	};

	// 0..5 = {Left, Right, Bottom, Top, Near, Far}
	enum class FrustumPlane : int { Left = 0, Right, Bottom, Top, Near, Far };

	struct Frustumf {
		std::array<Planef, 6> p{};

		void Normalize() noexcept {
			for (auto& pl : p) pl.Normalize();
		}

		// Row-major 4x4（DirectX想定）: m[16] = row-major 配列
		static Frustumf FromRowMajor(const float m[16], bool normalize = true) noexcept {
			auto M = [&](int r, int c) -> float { return m[r * 4 + c]; };
			auto make = [normalize](float a, float b, float c, float d) {
				return Planef::FromCoefficients(a, b, c, d, normalize);
				};
			Frustumf fr;
			// Left / Right
			fr.p[(int)FrustumPlane::Left] = make(M(0, 3) + M(0, 0), M(1, 3) + M(1, 0), M(2, 3) + M(2, 0), M(3, 3) + M(3, 0));
			fr.p[(int)FrustumPlane::Right] = make(M(0, 3) - M(0, 0), M(1, 3) - M(1, 0), M(2, 3) - M(2, 0), M(3, 3) - M(3, 0));
			// Bottom / Top
			fr.p[(int)FrustumPlane::Bottom] = make(M(0, 3) + M(0, 1), M(1, 3) + M(1, 1), M(2, 3) + M(2, 1), M(3, 3) + M(3, 1));
			fr.p[(int)FrustumPlane::Top] = make(M(0, 3) - M(0, 1), M(1, 3) - M(1, 1), M(2, 3) - M(2, 1), M(3, 3) - M(3, 1));
			// Near / Far
			fr.p[(int)FrustumPlane::Near] = make(M(0, 2), M(1, 2), M(2, 2), M(3, 2));
			fr.p[(int)FrustumPlane::Far] = make(M(0, 3) - M(0, 2), M(1, 3) - M(1, 2), M(2, 3) - M(2, 2), M(3, 3) - M(3, 2));
			return fr;
		}

		static Frustumf FromColMajor(const float m[16], bool normalize = true) noexcept {
			auto make = [normalize](float a, float b, float c, float d) {
				return Planef::FromCoefficients(a, b, c, d, normalize);
				};
			auto R = [&](int r, int c)->float { return m[r * 4 + c]; }; // 行読み（Row-major）
			auto col = [&](int c)->std::array<float, 4> {
				return { R(0,c), R(1,c), R(2,c), R(3,c) };
				};

			Frustumf fr;
			// 列抽出（col4±colX, Near=col3, Far=col4-col3）
			const auto c1 = col(0), c2 = col(1), c3 = col(2), c4 = col(3);
			fr.p[(int)FrustumPlane::Left] = make(c4[0] + c1[0], c4[1] + c1[1], c4[2] + c1[2], c4[3] + c1[3]);
			fr.p[(int)FrustumPlane::Right] = make(c4[0] - c1[0], c4[1] - c1[1], c4[2] - c1[2], c4[3] - c1[3]);
			fr.p[(int)FrustumPlane::Bottom] = make(c4[0] + c2[0], c4[1] + c2[1], c4[2] + c2[2], c4[3] + c2[3]);
			fr.p[(int)FrustumPlane::Top] = make(c4[0] - c2[0], c4[1] - c2[1], c4[2] - c2[2], c4[3] - c2[3]);
			fr.p[(int)FrustumPlane::Near] = make(c3[0], c3[1], c3[2], c3[3]);
			fr.p[(int)FrustumPlane::Far] = make(c4[0] - c3[0], c4[1] - c3[1], c4[2] - c3[2], c4[3] - c3[3]);
			return fr;
		}

		// 任意：面の向きを「内側=正」に揃える（判定式 s+r<0 と整合させる保険）
		static void FaceInward(Frustumf& fr,
			const Vec3f& camPos,
			const Vec3f& camFwd,
			float nearClip) noexcept
		{
			Vec3f inside = { camPos.x + camFwd.x * (std::max)(nearClip * 2.0f, 0.01f),
							 camPos.y + camFwd.y * (std::max)(nearClip * 2.0f, 0.01f),
							 camPos.z + camFwd.z * (std::max)(nearClip * 2.0f, 0.01f) };
			for (auto& pl : fr.p) {
				if (pl.SignedDistance(inside) < 0.0f) {
					pl.n.x = -pl.n.x; pl.n.y = -pl.n.y; pl.n.z = -pl.n.z; pl.d = -pl.d;
				}
			}
		}

		// ヘルパ：セル中心 (x,z) におけるフラスタムの縦方向可視範囲を求め、
		// [ymin,ymax] と交差させて「実効的 centerY/extentY」を返す。
		// 交差が無ければ false を返す。
		static inline bool ComputeYOverlapAtXZ(const Math::Frustumf& fr,
			float x, float z,
			float ymin, float ymax,
			float& outCenterY, float& outExtentY) noexcept
		{
			// 入力の順序が逆でも安全に
			if (ymin > ymax) std::swap(ymin, ymax);

			const auto& top = fr.p[(int)Math::FrustumPlane::Top];
			const auto& bottom = fr.p[(int)Math::FrustumPlane::Bottom];

			constexpr float EPS = 1e-6f;

			// y = -(nx*x + nz*z + d) / ny
			auto solveY = [&](const Math::Planef& pl)->float {
				if (std::fabs(pl.n.y) < EPS) {
					// ほぼ水平制約が無い（= この平面では Y を縛れない）
					// 上界/下界を表すために ±∞ を返しておく
					return (pl.n.y >= 0.f) ? std::numeric_limits<float>::infinity()
						: -std::numeric_limits<float>::infinity();
				}
				return -(pl.n.x * x + pl.n.z * z + pl.d) / pl.n.y;
				};

			float yTop = solveY(top);
			float yBottom = solveY(bottom);

			// フラスタムの Y 範囲（順序正規化）
			float yFmin = (std::min)(yTop, yBottom);
			float yFmax = (std::max)(yTop, yBottom);

			// 入力スラブと交差
			float y0 = (std::max)(ymin, yFmin);
			float y1 = (std::min)(ymax, yFmax);
			if (y0 > y1) return false; // そもそもその (x,z) で縦に重ならない

			outCenterY = 0.5f * (y0 + y1);
			outExtentY = 0.5f * (y1 - y0);
			return true;
		}

		// 点
		bool ContainsPoint(const Vec3f& pt) const noexcept {
			for (const auto& pl : p) if (pl.SignedDistance(pt) < 0.f) return false;
			return true;
		}

		// 球
		bool IntersectsSphere(const Vec3f& c, float r) const noexcept {
			for (const auto& pl : p) if (pl.SignedDistance(c) < -r) return false;
			return true;
		}

		// AABB: center/extents 版
		bool IntersectsAABB(const Vec3f& center, const Vec3f& extent) const noexcept {
			for (const auto& pl : p) {
				const float s = pl.n.x * center.x + pl.n.y * center.y + pl.n.z * center.z + pl.d;
				const float r = std::fabs(pl.n.x) * extent.x + std::fabs(pl.n.y) * extent.y + std::fabs(pl.n.z) * extent.z;
				if (s + r < 0.f) return false; // 完全外
			}
			return true;
		}

		// AABB: lb/ub 版（AABB3f がある場合）
		template<class AABB3>
		bool IntersectsAABB(const AABB3& aabb) const noexcept {
			const Vec3f c{ (aabb.lb.x + aabb.ub.x) * 0.5f,
						   (aabb.lb.y + aabb.ub.y) * 0.5f,
						   (aabb.lb.z + aabb.ub.z) * 0.5f };
			const Vec3f e{ (aabb.ub.x - aabb.lb.x) * 0.5f,
						   (aabb.ub.y - aabb.lb.y) * 0.5f,
						   (aabb.ub.z - aabb.lb.z) * 0.5f };
			return IntersectsAABB(c, e);
		}
	};
} // namespace SectorFW::Math