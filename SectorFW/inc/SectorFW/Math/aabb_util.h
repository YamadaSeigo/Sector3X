#pragma once
#include <cstddef>
#include <limits>
#include <cmath>
#include <vector>
#include "Vector.hpp"   // Vec3f
#include "AABB.hpp"     // AABB3f

#include "../Debug/DebugType.h"

#if defined(_MSC_VER)
#include <immintrin.h>   // SSE2/AVX2 (MSVC x64 はSSE2常時有効)
#elif defined(__SSE2__)
#include <immintrin.h>
#endif

namespace SectorFW::Math {

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
		const __m128 pos_inf = _mm_set1_ps(std::numeric_limits<float>::infinity());

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
		const __m128 pos_inf = _mm_set1_ps(std::numeric_limits<float>::infinity());

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

	inline AABB3f MakeAABB(const std::vector<Vec3f>& positions, std::vector<uint32_t> indices) noexcept {
		return MakeAABB_FromAoSWithIndex(positions.data(), positions.size(), indices.data(), indices.size(), sizeof(Vec3f));
	}

	/// AABB3f を 12 本のライン用 24 頂点で返す（重複あり・インデックス不要）
	inline std::array<Debug::LineVertex, 24> MakeAABBLineVertices(const AABB3f& box, uint32_t rgba = 0xFFFFFFFF)
	{
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
		const AABB3f& box,
		uint32_t rgba = 0xFFFFFFFF)
	{
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

} // namespace SectorFW::Math
