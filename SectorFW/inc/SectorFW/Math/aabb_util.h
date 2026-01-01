#pragma once
#include <cstddef>
#include <limits>
#include <cmath>
#include <vector>
#include "Vector.hpp"   // Vec3f
#include "AABB.hpp"     // AABB3f
#include "Matrix.hpp"


#if defined(_MSC_VER)
#include <immintrin.h>   // SSE2/AVX2 (MSVC x64 はSSE2常時有効)
#elif defined(__SSE2__)
#include <immintrin.h>
#endif

namespace SFW::Math {

	//-------------------------------
	// 共通ヘルパ
	//-------------------------------
	inline float SanitizeFinite(float v) noexcept {
		// 必要なければそのまま返す。NaN/Inf を弾きたい場合はコメントアウト解除
		// return std::isfinite(v) ? v : 0.0f;
		return v;
	}

	//===============================
	// AoS: Vec3f の配列からAABB
	//===============================
	inline AABB3f MakeAABB_FromAoS(const Vec3f* positions, size_t count, size_t strideBytes = sizeof(Vec3f)) noexcept
	{
		AABB3f out;
		if (!positions || count == 0) {
			out.lb = { 0,0,0 };
			out.ub = { 0,0,0 };
			return out;
		}

		// 初期値
		float minx = std::numeric_limits<float>::infinity();
		float miny = std::numeric_limits<float>::infinity();
		float minz = std::numeric_limits<float>::infinity();
		float maxx = -std::numeric_limits<float>::infinity();
		float maxy = -std::numeric_limits<float>::infinity();
		float maxz = -std::numeric_limits<float>::infinity();

		const unsigned char* base = reinterpret_cast<const unsigned char*>(positions);

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
		// SSE2: 3軸を同時に更新（1頂点/ループ）
		__m128 vmin = _mm_set1_ps(std::numeric_limits<float>::infinity());
		__m128 vmax = _mm_set1_ps(-std::numeric_limits<float>::infinity());

		for (size_t i = 0; i < count; ++i) {
			const Vec3f* p = reinterpret_cast<const Vec3f*>(base + i * strideBytes);
			// 安全のため各成分はスカラーロードし、w は +inf（min 計算で影響を与えない）にする
			const float x = SanitizeFinite(p->x);
			const float y = SanitizeFinite(p->y);
			const float z = SanitizeFinite(p->z);

			__m128 v = _mm_set_ps(std::numeric_limits<float>::infinity(), z, y, x);
			vmin = _mm_min_ps(vmin, v);
			vmax = _mm_max_ps(vmax, v);
		}
		// 結果を取り出し
		alignas(16) float mins[4], maxs[4];
		_mm_store_ps(mins, vmin);
		_mm_store_ps(maxs, vmax);
		minx = (std::min)(minx, mins[0]);  maxx = (std::max)(maxx, maxs[0]); // x
		miny = (std::min)(miny, mins[1]);  maxy = (std::max)(maxy, maxs[1]); // y
		minz = (std::min)(minz, mins[2]);  maxz = (std::max)(maxz, maxs[2]); // z

#else
		// スカラー版
		for (size_t i = 0; i < count; ++i) {
			const Vec3f* p = reinterpret_cast<const Vec3f*>(base + i * strideBytes);
			const float x = SanitizeFinite(p->x);
			const float y = SanitizeFinite(p->y);
			const float z = SanitizeFinite(p->z);
			minx = (std::min)(minx, x); maxx = (std::max)(maxx, x);
			miny = (std::min)(miny, y); maxy = (std::max)(maxy, y);
			minz = (std::min)(minz, z); maxz = (std::max)(maxz, z);
		}
#endif

		out.lb = { minx, miny, minz };
		out.ub = { maxx, maxy, maxz };
		return out;
	}

	//===============================
	// AoS: Vec3f の配列からAABB
	//===============================
	inline AABB3f MakeAABB_FromAoSWithIndex(const Vec3f* positions, size_t posCount, const uint32_t* indices, size_t idxCount, size_t strideBytes = sizeof(Vec3f)) noexcept
	{
		AABB3f out;
		if (!positions || !indices || idxCount == 0) {
			out.lb = { 0,0,0 };
			out.ub = { 0,0,0 };
			return out;
		}

		// 初期値
		float minx = std::numeric_limits<float>::infinity();
		float miny = std::numeric_limits<float>::infinity();
		float minz = std::numeric_limits<float>::infinity();
		float maxx = -std::numeric_limits<float>::infinity();
		float maxy = -std::numeric_limits<float>::infinity();
		float maxz = -std::numeric_limits<float>::infinity();

		const unsigned char* base = reinterpret_cast<const unsigned char*>(positions);

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
		// SSE2: 3軸を同時に更新（1頂点/ループ）
		__m128 vmin = _mm_set1_ps(std::numeric_limits<float>::infinity());
		__m128 vmax = _mm_set1_ps(-std::numeric_limits<float>::infinity());

		for (size_t i = 0; i < idxCount; ++i) {
			if (indices[i] >= posCount) [[unlikely]] continue; // 念のため範囲チェック

			const Vec3f* p = reinterpret_cast<const Vec3f*>(base + indices[i] * strideBytes);
			// 安全のため各成分はスカラーロードし、w は +inf（min 計算で影響を与えない）にする
			const float x = SanitizeFinite(p->x);
			const float y = SanitizeFinite(p->y);
			const float z = SanitizeFinite(p->z);

			__m128 v = _mm_set_ps(std::numeric_limits<float>::infinity(), z, y, x);
			vmin = _mm_min_ps(vmin, v);
			vmax = _mm_max_ps(vmax, v);
		}
		// 結果を取り出し
		alignas(16) float mins[4], maxs[4];
		_mm_store_ps(mins, vmin);
		_mm_store_ps(maxs, vmax);
		minx = (std::min)(minx, mins[0]);  maxx = (std::max)(maxx, maxs[0]); // x
		miny = (std::min)(miny, mins[1]);  maxy = (std::max)(maxy, maxs[1]); // y
		minz = (std::min)(minz, mins[2]);  maxz = (std::max)(maxz, maxs[2]); // z

#else
		// スカラー版
		for (size_t i = 0; i < idxCount; ++i) {
			if (indices[i] >= posCount) continue;
			const Vec3f* p = reinterpret_cast<const Vec3f*>(base + indices[i] * strideBytes);
			const float x = SanitizeFinite(p->x);
			const float y = SanitizeFinite(p->y);
			const float z = SanitizeFinite(p->z);
			minx = (std::min)(minx, x); maxx = (std::max)(maxx, x);
			miny = (std::min)(miny, y); maxy = (std::max)(maxy, y);
			minz = (std::min)(minz, z); maxz = (std::max)(maxz, z);
		}
#endif

		out.lb = { minx, miny, minz };
		out.ub = { maxx, maxy, maxz };
		return out;
	}

	//===============================
	// SoA: x[], y[], z[] からAABB
	//===============================
	inline AABB3f MakeAABB_FromSoA(const float* xs, const float* ys, const float* zs, size_t count) noexcept
	{
		AABB3f out;
		if (!xs || !ys || !zs || count == 0) {
			out.lb = { 0,0,0 };
			out.ub = { 0,0,0 };
			return out;
		}

		float minx = std::numeric_limits<float>::infinity();
		float miny = std::numeric_limits<float>::infinity();
		float minz = std::numeric_limits<float>::infinity();
		float maxx = -std::numeric_limits<float>::infinity();
		float maxy = -std::numeric_limits<float>::infinity();
		float maxz = -std::numeric_limits<float>::infinity();

#if (defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))) && !defined(__arm64__)
		// AVX2: 8要素ずつ
		const size_t step = 8;
		size_t i = 0;

		__m256 xminv = _mm256_set1_ps(std::numeric_limits<float>::infinity());
		__m256 yminv = _mm256_set1_ps(std::numeric_limits<float>::infinity());
		__m256 zminv = _mm256_set1_ps(std::numeric_limits<float>::infinity());
		__m256 xmaxv = _mm256_set1_ps(-std::numeric_limits<float>::infinity());
		__m256 ymaxv = _mm256_set1_ps(-std::numeric_limits<float>::infinity());
		__m256 zmaxv = _mm256_set1_ps(-std::numeric_limits<float>::infinity());

		for (; i + step <= count; i += step) {
			__m256 vx = _mm256_loadu_ps(xs + i);
			__m256 vy = _mm256_loadu_ps(ys + i);
			__m256 vz = _mm256_loadu_ps(zs + i);

			xminv = _mm256_min_ps(xminv, vx);
			yminv = _mm256_min_ps(yminv, vy);
			zminv = _mm256_min_ps(zminv, vz);
			xmaxv = _mm256_max_ps(xmaxv, vx);
			ymaxv = _mm256_max_ps(ymaxv, vy);
			zmaxv = _mm256_max_ps(zmaxv, vz);
		}
		// 水平縮約
		alignas(32) float xmins[8], ymins[8], zmins[8], xmaxs[8], ymaxs[8], zmaxs[8];
		_mm256_store_ps(xmins, xminv);
		_mm256_store_ps(ymins, yminv);
		_mm256_store_ps(zmins, zminv);
		_mm256_store_ps(xmaxs, xmaxv);
		_mm256_store_ps(ymaxs, ymaxv);
		_mm256_store_ps(zmaxs, zmaxv);
		for (int k = 0; k < 8; ++k) {
			minx = (std::min)(minx, xmins[k]); maxx = (std::max)(maxx, xmaxs[k]);
			miny = (std::min)(miny, ymins[k]); maxy = (std::max)(maxy, ymaxs[k]);
			minz = (std::min)(minz, zmins[k]); maxz = (std::max)(maxz, zmaxs[k]);
		}
		// 端数
		for (; i < count; ++i) {
			const float x = SanitizeFinite(xs[i]);
			const float y = SanitizeFinite(ys[i]);
			const float z = SanitizeFinite(zs[i]);
			minx = (std::min)(minx, x); maxx = (std::max)(maxx, x);
			miny = (std::min)(miny, y); maxy = (std::max)(maxy, y);
			minz = (std::min)(minz, z); maxz = (std::max)(maxz, z);
		}

#else
		// SSE2 またはスカラー
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
		const size_t step = 4;
		size_t i = 0;
		__m128 xminv = _mm_set1_ps(std::numeric_limits<float>::infinity());
		__m128 yminv = _mm_set1_ps(std::numeric_limits<float>::infinity());
		__m128 zminv = _mm_set1_ps(std::numeric_limits<float>::infinity());
		__m128 xmaxv = _mm_set1_ps(-std::numeric_limits<float>::infinity());
		__m128 ymaxv = _mm_set1_ps(-std::numeric_limits<float>::infinity());
		__m128 zmaxv = _mm_set1_ps(-std::numeric_limits<float>::infinity());
		for (; i + step <= count; i += step) {
			__m128 vx = _mm_loadu_ps(xs + i);
			__m128 vy = _mm_loadu_ps(ys + i);
			__m128 vz = _mm_loadu_ps(zs + i);
			xminv = _mm_min_ps(xminv, vx);
			yminv = _mm_min_ps(yminv, vy);
			zminv = _mm_min_ps(zminv, vz);
			xmaxv = _mm_max_ps(xmaxv, vx);
			ymaxv = _mm_max_ps(ymaxv, vy);
			zmaxv = _mm_max_ps(zmaxv, vz);
		}
		alignas(16) float xmins[4], ymins[4], zmins[4], xmaxs[4], ymaxs[4], zmaxs[4];
		_mm_store_ps(xmins, xminv);
		_mm_store_ps(ymins, yminv);
		_mm_store_ps(zmins, zminv);
		_mm_store_ps(xmaxs, xmaxv);
		_mm_store_ps(ymaxs, ymaxv);
		_mm_store_ps(zmaxs, zmaxv);
		for (int k = 0; k < 4; ++k) {
			minx = (std::min)(minx, xmins[k]); maxx = (std::max)(maxx, xmaxs[k]);
			miny = (std::min)(miny, ymins[k]); maxy = (std::max)(maxy, ymaxs[k]);
			minz = (std::min)(minz, zmins[k]); maxz = (std::max)(maxz, zmaxs[k]);
		}
		for (; i < count; ++i) {
			const float x = SanitizeFinite(xs[i]);
			const float y = SanitizeFinite(ys[i]);
			const float z = SanitizeFinite(zs[i]);
			minx = (std::min)(minx, x); maxx = (std::max)(maxx, x);
			miny = (std::min)(miny, y); maxy = (std::max)(maxy, y);
			minz = (std::min)(minz, z); maxz = (std::max)(maxz, z);
		}
#else
  // 純スカラー
		for (size_t i = 0; i < count; ++i) {
			const float x = SanitizeFinite(xs[i]);
			const float y = SanitizeFinite(ys[i]);
			const float z = SanitizeFinite(zs[i]);
			minx = (std::min)(minx, x); maxx = (std::max)(maxx, x);
			miny = (std::min)(miny, y); maxy = (std::max)(maxy, y);
			minz = (std::min)(minz, z); maxz = (std::max)(maxz, z);
		}
#endif
#endif

		out.lb = { minx, miny, minz };
		out.ub = { maxx, maxy, maxz };
		return out;
	}

	//===============================
	// ラッパ：std::vector<Vec3f> から
	//===============================
	inline AABB3f MakeAABB(const std::vector<Vec3f>& positions) noexcept {
		return MakeAABB_FromAoS(positions.data(), positions.size(), sizeof(Vec3f));
	}

	inline AABB3f MakeAABB(const std::vector<Vec3f>& positions, const std::vector<uint32_t>& indices) noexcept {
		return MakeAABB_FromAoSWithIndex(positions.data(), positions.size(), indices.data(), indices.size(), sizeof(Vec3f));
	}


	/// ついでに 8 コーナーだけ欲しい場合
	inline std::array<Vec3f, 8> AABBCorners(const AABB3f& box)
	{
		const auto& lb = box.lb;
		const auto& ub = box.ub;
		return {
			Vec3f{ lb.x, lb.y, lb.z },
			Vec3f{ ub.x, lb.y, lb.z },
			Vec3f{ ub.x, ub.y, lb.z },
			Vec3f{ lb.x, ub.y, lb.z },
			Vec3f{ lb.x, lb.y, ub.z },
			Vec3f{ ub.x, lb.y, ub.z },
			Vec3f{ ub.x, ub.y, ub.z },
			Vec3f{ lb.x, ub.y, ub.z }
		};
	}

	//======================================================================
	// AABB × 行列（列ベクトル規約 / row-major 保管 / 右列=平行移動）
	// 4x4版と3x4版（48Bワールド行列）を用意
	//   ※アフィン限定。透視（Wが変わる）は8頂点変換が必要。
	//======================================================================

	template<class T>
	inline AABB<T, Vec3<T>>
		TransformAABB_Affine(const Matrix<4, 4, T>& M, const AABB<T, Vec3<T>>& box) noexcept
	{
		// 中心と半径（extent）
		const Vec3<T> c = (box.lb + box.ub) * T(0.5);
		const Vec3<T> e = (box.ub - box.lb) * T(0.5);

		// 列ベクトル規約なので x' = dot(row0.xyz, c) + M[0][3] などで OK
		const T cx = M[0][0] * c.x + M[0][1] * c.y + M[0][2] * c.z + M[0][3];
		const T cy = M[1][0] * c.x + M[1][1] * c.y + M[1][2] * c.z + M[1][3];
		const T cz = M[2][0] * c.x + M[2][1] * c.y + M[2][2] * c.z + M[2][3];

		// extent は線形部の絶対値を使って押し広げる
		const T ex = std::abs(M[0][0]) * e.x + std::abs(M[0][1]) * e.y + std::abs(M[0][2]) * e.z;
		const T ey = std::abs(M[1][0]) * e.x + std::abs(M[1][1]) * e.y + std::abs(M[1][2]) * e.z;
		const T ez = std::abs(M[2][0]) * e.x + std::abs(M[2][1]) * e.y + std::abs(M[2][2]) * e.z;

		AABB<T, Vec3<T>> out;
		out.lb = { cx - ex, cy - ey, cz - ez };
		out.ub = { cx + ex, cy + ey, cz + ez };
		return out;
	}

	// 48B（3x4）ワールド行列にもそのまま対応
	template<class T>
	inline AABB<T, Vec3<T>>
		TransformAABB_Affine(const Matrix<3, 4, T>& M, const AABB<T, Vec3<T>>& box) noexcept
	{
		const Vec3<T> c = (box.lb + box.ub) * T(0.5);
		const Vec3<T> e = (box.ub - box.lb) * T(0.5);

		const T cx = M[0][0] * c.x + M[0][1] * c.y + M[0][2] * c.z + M[0][3];
		const T cy = M[1][0] * c.x + M[1][1] * c.y + M[1][2] * c.z + M[1][3];
		const T cz = M[2][0] * c.x + M[2][1] * c.y + M[2][2] * c.z + M[2][3];

		const T ex = std::abs(M[0][0]) * e.x + std::abs(M[0][1]) * e.y + std::abs(M[0][2]) * e.z;
		const T ey = std::abs(M[1][0]) * e.x + std::abs(M[1][1]) * e.y + std::abs(M[1][2]) * e.z;
		const T ez = std::abs(M[2][0]) * e.x + std::abs(M[2][1]) * e.y + std::abs(M[2][2]) * e.z;

		AABB<T, Vec3<T>> out;
		out.lb = { cx - ex, cy - ey, cz - ez };
		out.ub = { cx + ex, cy + ey, cz + ez };
		return out;
	}

} // namespace SectorFW::Math
