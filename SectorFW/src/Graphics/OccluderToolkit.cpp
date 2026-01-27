// OccluderToolkit.cpp
// Implementation that hides melt.h and hosts SIMD/AVX2 intrinsics.
// -----------------------------------------------------------------------------
#include "Graphics/OccluderToolkit.h"

#include "Math/convert.hpp"

#include <limits>
#include <unordered_map>
#include <cstring>   // memset
#include <immintrin.h>

#include "Debug/logger.h"

using namespace SFW::Graphics;
using SFW::Math::Vec3f;
using SFW::Math::Vec4f;
using Mat4f = SFW::Math::Matrix4x4f;
using SFW::Math::AABB3f;

// SIMD feature flags (compile-time)
#if defined(__SSE2__) || defined(_M_X64)
#define SFW_HAVE_SSE2 1
#else
#define SFW_HAVE_SSE2 0
#endif
#if defined(__AVX2__)
#define SFW_HAVE_AVX2 1
#else
#define SFW_HAVE_AVX2 0
#endif

// ---------- melt is hidden in this .cpp ----------
#ifndef HAVE_MELT
#if defined(__has_include)
#if __has_include("../external/melt/melt.h")
#define HAVE_MELT 1
#else
#define HAVE_MELT 0
#endif
#else
#define HAVE_MELT 0
#endif
#endif

#if HAVE_MELT
#define MELT_IMPLEMENTATION
extern "C" {
# include "../external/melt/melt.h"
}
#endif

#ifndef SFW_MATH_ROWVEC
#  define SFW_MATH_ROWVEC 0  // 1 = v*M 規約（行ベクトル）, 0 = M*v 規約（列ベクトル）
#endif

// ---------- Small helpers ----------
static inline bool IsInsideAABB_Local(const AABB3f& b, const Vec3f& p) {
	return (p.x >= b.lb.x && p.x <= b.ub.x) &&
		(p.y >= b.lb.y && p.y <= b.ub.y) &&
		(p.z >= b.lb.z && p.z <= b.ub.z);
}

static inline Vec4f MulMat4Vec4(const Mat4f& M, const Vec4f& v)
{
#ifdef SFW_ROWMAJOR_MAT4F_HAS_M
	return Vec4f(
		M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z + M.m[0][3] * v.w,
		M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z + M.m[1][3] * v.w,
		M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z + M.m[2][3] * v.w,
		M.m[3][0] * v.x + M.m[3][1] * v.y + M.m[3][2] * v.z + M.m[3][3] * v.w
	);
#else
	// fall back to user's operator*
	return M * v;
#endif
}

static inline Vec4f MulPointClip_ByVP(const Mat4f& VP, const Vec4f& v)
{
#ifdef SFW_ROWMAJOR_MAT4F_HAS_M
#  if SFW_MATH_ROWVEC
	// v*M（行ベクトル）: 各出力成分は「列」を使う
	float cx = v.x * VP.m[0][0] + v.y * VP.m[1][0] + v.z * VP.m[2][0] + v.w * VP.m[3][0];
	float cy = v.x * VP.m[0][1] + v.y * VP.m[1][1] + v.z * VP.m[2][1] + v.w * VP.m[3][1];
	float cz = v.x * VP.m[0][2] + v.y * VP.m[1][2] + v.z * VP.m[2][2] + v.w * VP.m[3][2];
	float cw = v.x * VP.m[0][3] + v.y * VP.m[1][3] + v.z * VP.m[2][3] + v.w * VP.m[3][3];
	return Vec4f(cx, cy, cz, cw);
#  else
	// M*v（列ベクトル）: 従来どおり「行」を使う
	return Vec4f(
		VP.m[0][0] * v.x + VP.m[0][1] * v.y + VP.m[0][2] * v.z + VP.m[0][3] * v.w,
		VP.m[1][0] * v.x + VP.m[1][1] * v.y + VP.m[1][2] * v.z + VP.m[1][3] * v.w,
		VP.m[2][0] * v.x + VP.m[2][1] * v.y + VP.m[2][2] * v.z + VP.m[2][3] * v.w,
		VP.m[3][0] * v.x + VP.m[3][1] * v.y + VP.m[3][2] * v.z + VP.m[3][3] * v.w
	);
#  endif
#else
	// 直接アクセスできない型なら、演算子に合わせて呼び分けるしかない（必要ならこちらを調整）
	return VP * v; // ここは従来の列ベクトル仮定
#endif
}

// -----------------------------------------------------------------------------
// (A) melt integration (optional)
// -----------------------------------------------------------------------------
MeltBuildStatus SFW::Graphics::GenerateOccluderAABBs_MaybeWithMelt(
	const std::vector<Vec3f>& positions,
	const std::vector<uint32_t>& indices,
	int   meltResolution,
	float meltFillPct,
	std::vector<AABB3f>& outAABBs,
	MeltBoxType boxType)
{
	auto pushWhole = [&]() {
		if (positions.empty()) return;
		Vec3f lb(+std::numeric_limits<float>::infinity());
		Vec3f ub(-std::numeric_limits<float>::infinity());
		for (auto& p : positions) {
			lb.x = (std::min)(lb.x, p.x); lb.y = (std::min)(lb.y, p.y); lb.z = (std::min)(lb.z, p.z);
			ub.x = (std::max)(ub.x, p.x); ub.y = (std::max)(ub.y, p.y); ub.z = (std::max)(ub.z, p.z);
		}
		outAABBs.push_back({ lb, ub });
		};

#if HAVE_MELT
	if (positions.empty() || indices.size() < 3) {
		pushWhole();
		return positions.empty() ? MeltBuildStatus::Failed : MeltBuildStatus::FallbackWhole;
	}
	if (positions.size() > 65535) {
		pushWhole(); // needs 16-bit remap for melt
		return MeltBuildStatus::FallbackWhole;
	}

	std::vector<melt_vec3_t> vs(positions.size());
	for (size_t i = 0; i < positions.size(); ++i) {
		vs[i].x = positions[i].x; vs[i].y = positions[i].y; vs[i].z = positions[i].z;
	}
	std::vector<uint16_t> is; is.reserve(indices.size());
	for (uint32_t idx : indices) is.push_back(static_cast<uint16_t>(idx));

	Vec3f lb(+std::numeric_limits<float>::infinity());
	Vec3f ub(-std::numeric_limits<float>::infinity());
	for (auto& p : positions) {
		lb.x = (std::min)(lb.x, p.x); lb.y = (std::min)(lb.y, p.y); lb.z = (std::min)(lb.z, p.z);
		ub.x = (std::max)(ub.x, p.x); ub.y = (std::max)(ub.y, p.y); ub.z = (std::max)(ub.z, p.z);
	}
	Vec3f extent = ub - lb;
	float maxDim = (std::max)({ extent.x, extent.y, extent.z });
	int   res = (std::max)(4, meltResolution > 0 ? meltResolution : 64);
	float voxelSize = (maxDim > 0.f) ? (maxDim / float(res)) : 1.0f;

	const uint64_t MAX_VOXELS = 2'000'000ull;
	auto dim = [&](float vs)->std::array<uint32_t, 3> {
		uint32_t dx = (uint32_t)std::ceil(extent.x / vs) + 2;
		uint32_t dy = (uint32_t)std::ceil(extent.y / vs) + 2;
		uint32_t dz = (uint32_t)std::ceil(extent.z / vs) + 2;
		return { dx,dy,dz };
		};
	auto d = dim(voxelSize);
	while ((uint64_t)d[0] * d[1] * d[2] > MAX_VOXELS) {
		voxelSize *= 2.0f;
		d = dim(voxelSize);
	}

	melt_params_t params; std::memset(&params, 0, sizeof(params));
	params.mesh.vertices = vs.data();
	params.mesh.indices = is.data();
	params.mesh.vertex_count = (uint32_t)vs.size();
	params.mesh.index_count = (uint32_t)is.size();
	params.box_type_flags = (melt_occluder_box_type_flags_t)boxType;
	params.voxel_size = voxelSize;
	params.fill_pct = (std::max)(0.05f, (std::min)(1.0f, meltFillPct));

	melt_result_t result{};
	if (!melt_generate_occluder(params, &result)) {
		pushWhole();
		return MeltBuildStatus::FallbackWhole;
	}

	constexpr uint32_t K = 8;
	if (!result.mesh.vertices || result.mesh.vertex_count < K) {
		melt_free_result(result);
		return MeltBuildStatus::Failed;
	}

	uint32_t boxCount = result.mesh.vertex_count / K;
	outAABBs.reserve(outAABBs.size() + boxCount);
	for (uint32_t i = 0; i < boxCount; ++i) {
		const melt_vec3_t* v = result.mesh.vertices + i * K;
		float minx = v[0].x, miny = v[0].y, minz = v[0].z;
		float maxx = v[0].x, maxy = v[0].y, maxz = v[0].z;
		for (uint32_t k = 1; k < K; ++k) {
			minx = (std::min)(minx, v[k].x); miny = (std::min)(miny, v[k].y); minz = (std::min)(minz, v[k].z);
			maxx = (std::max)(maxx, v[k].x); maxy = (std::max)(maxy, v[k].y); maxz = (std::max)(maxz, v[k].z);
		}
		outAABBs.push_back({ Vec3f(minx,miny,minz), Vec3f(maxx,maxy,maxz) });
	}
	melt_free_result(result);
	return MeltBuildStatus::UsedMelt;
#else
	(void)meltResolution; (void)meltFillPct;
	pushWhole();
	return positions.empty() ? MeltBuildStatus::Failed : MeltBuildStatus::FallbackWhole;
#endif
}

// -----------------------------------------------------------------------------
// (B) front-face quad (O(1)), AABB helpers
// -----------------------------------------------------------------------------
bool SFW::Graphics::ComputeFrontFaceQuad(const AABB3f& b, const Vec3f& camPos, AABBFrontFaceQuad& out, AABBQuadAxisBit axisBit)
{
	const float ex = b.ub.x - b.lb.x;
	const float ey = b.ub.y - b.lb.y;
	const float ez = b.ub.z - b.lb.z;
	const float eps = 1e-6f;
	if (ex < eps || ey < eps || ez < eps) return false;
	if (IsInsideAABB_Local(b, camPos)) return false;

	const Vec3f c = (b.lb + b.ub) * 0.5f;
	const Vec3f d = camPos - c;

	float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
	AABBQuadAxisBit axis = AABBQuadAxisBit::None;
	float amax = 0.0f;
	if (axisBit & AABBQuadAxisBit::X && ax > amax) { axis = AABBQuadAxisBit::X; amax = ax; }
	if (axisBit & AABBQuadAxisBit::Y && ay > amax) { axis = AABBQuadAxisBit::Y; amax = ay; }
	if (axisBit & AABBQuadAxisBit::Z && az > amax) { axis = AABBQuadAxisBit::Z; }

	if (axis == AABBQuadAxisBit::None) return false;

	bool positive = true;
	if (axis == AABBQuadAxisBit::X) positive = (d.x >= 0.0f);
	else if (axis == AABBQuadAxisBit::Y) positive = (d.y >= 0.0f);
	else                positive = (d.z >= 0.0f);

	Vec3f v[4]; Vec3f n(0, 0, 0);
	if (axis == AABBQuadAxisBit::X) {
		n.x = positive ? +1.f : -1.f; float x = positive ? b.ub.x : b.lb.x;
		if (positive) { v[0] = { x,b.lb.y,b.lb.z }; v[1] = { x,b.ub.y,b.lb.z }; v[2] = { x,b.ub.y,b.ub.z }; v[3] = { x,b.lb.y,b.ub.z }; }
		else { v[0] = { x,b.lb.y,b.ub.z }; v[1] = { x,b.ub.y,b.ub.z }; v[2] = { x,b.ub.y,b.lb.z }; v[3] = { x,b.lb.y,b.lb.z }; }
	}
	else if (axis == AABBQuadAxisBit::Y) {
		n.y = positive ? +1.f : -1.f; float y = positive ? b.ub.y : b.lb.y;
		if (positive) { v[0] = { b.lb.x,y,b.lb.z }; v[1] = { b.ub.x,y,b.lb.z }; v[2] = { b.ub.x,y,b.ub.z }; v[3] = { b.lb.x,y,b.ub.z }; }
		else { v[0] = { b.lb.x,y,b.ub.z }; v[1] = { b.ub.x,y,b.ub.z }; v[2] = { b.ub.x,y,b.lb.z }; v[3] = { b.lb.x,y,b.lb.z }; }
	}
	else {
		n.z = positive ? +1.f : -1.f; float z = positive ? b.ub.z : b.lb.z;
		if (positive) { v[0] = { b.lb.x,b.lb.y,z }; v[1] = { b.ub.x,b.lb.y,z }; v[2] = { b.ub.x,b.ub.y,z }; v[3] = { b.lb.x,b.ub.y,z }; }
		else { v[0] = { b.lb.x,b.ub.y,z }; v[1] = { b.ub.x,b.ub.y,z }; v[2] = { b.ub.x,b.lb.y,z }; v[3] = { b.lb.x,b.lb.y,z }; }
	}

	for (int i = 0; i < 4; ++i) out.v[i] = v[i];
	out.normal = n; out.axis = axis; out.positive = positive;
	return true;
}

void SFW::Graphics::QuadToTrianglesCCW(uint16_t outIdx[6]) {
	outIdx[0] = 0; outIdx[1] = 1; outIdx[2] = 2;
	outIdx[3] = 0; outIdx[4] = 2; outIdx[5] = 3;
}

// -----------------------------------------------------------------------------
// (C) screen size measures + SIMD projection
// -----------------------------------------------------------------------------
float SFW::Graphics::EstimateMaxScreenDiameterPx(const AABB3f& b, const Vec3f& camPos, const OccluderViewport& vp)
{
	const Vec3f c = (b.lb + b.ub) * 0.5f;
	const Vec3f e = (b.ub - b.lb) * 0.5f;
	const float r = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z);
	const float z = (std::max)(1e-4f, (c - camPos).length());
	const float pixPerUnit = vp.height * 0.5f / std::tan((std::max)(1e-6f, vp.fovY * 0.5f));
	return (2.0f * r) * (pixPerUnit / z);
}

// SSE helpers
#if SFW_HAVE_SSE2 && defined(SFW_ROWMAJOR_MAT4F_HAS_M)
static inline float hmin4(__m128 v) {
	__m128 t = _mm_min_ps(v, _mm_movehl_ps(v, v));
	t = _mm_min_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 2, 0, 0)));
	return _mm_cvtss_f32(t);
}
static inline float hmax4(__m128 v) {
	__m128 t = _mm_max_ps(v, _mm_movehl_ps(v, v));
	t = _mm_max_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 2, 0, 0)));
	return _mm_cvtss_f32(t);
}

static inline void TransformPoints4_SSE(const float p[4][3], const Mat4f& VP,
	__m128& outX, __m128& outY, __m128& outZ, __m128& outW)
{
	const __m128 X = _mm_set_ps(p[3][0], p[2][0], p[1][0], p[0][0]);
	const __m128 Y = _mm_set_ps(p[3][1], p[2][1], p[1][1], p[0][1]);
	const __m128 Z = _mm_set_ps(p[3][2], p[2][2], p[1][2], p[0][2]);
	const __m128 ONE = _mm_set1_ps(1.0f);

#   if SFW_MATH_ROWVEC
	// v*M：各出力成分は「列」を使う
	const __m128 c0x = _mm_set1_ps(VP.m[0][0]), c1x = _mm_set1_ps(VP.m[1][0]), c2x = _mm_set1_ps(VP.m[2][0]), c3x = _mm_set1_ps(VP.m[3][0]);
	const __m128 c0y = _mm_set1_ps(VP.m[0][1]), c1y = _mm_set1_ps(VP.m[1][1]), c2y = _mm_set1_ps(VP.m[2][1]), c3y = _mm_set1_ps(VP.m[3][1]);
	const __m128 c0z = _mm_set1_ps(VP.m[0][2]), c1z = _mm_set1_ps(VP.m[1][2]), c2z = _mm_set1_ps(VP.m[2][2]), c3z = _mm_set1_ps(VP.m[3][2]);
	const __m128 c0w = _mm_set1_ps(VP.m[0][3]), c1w = _mm_set1_ps(VP.m[1][3]), c2w = _mm_set1_ps(VP.m[2][3]), c3w = _mm_set1_ps(VP.m[3][3]);

	outX = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, c0x), _mm_mul_ps(Y, c1x)),
		_mm_add_ps(_mm_mul_ps(Z, c2x), _mm_mul_ps(ONE, c3x)));
	outY = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, c0y), _mm_mul_ps(Y, c1y)),
		_mm_add_ps(_mm_mul_ps(Z, c2y), _mm_mul_ps(ONE, c3y)));
	outZ = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, c0z), _mm_mul_ps(Y, c1z)),
		_mm_add_ps(_mm_mul_ps(Z, c2z), _mm_mul_ps(ONE, c3z)));
	outW = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, c0w), _mm_mul_ps(Y, c1w)),
		_mm_add_ps(_mm_mul_ps(Z, c2w), _mm_mul_ps(ONE, c3w)));
#   else
	// M*v：従来どおり「行」を使う
	const __m128 r0x = _mm_set1_ps(VP.m[0][0]), r0y = _mm_set1_ps(VP.m[0][1]), r0z = _mm_set1_ps(VP.m[0][2]), r0w = _mm_set1_ps(VP.m[0][3]);
	const __m128 r1x = _mm_set1_ps(VP.m[1][0]), r1y = _mm_set1_ps(VP.m[1][1]), r1z = _mm_set1_ps(VP.m[1][2]), r1w = _mm_set1_ps(VP.m[1][3]);
	const __m128 r2x = _mm_set1_ps(VP.m[2][0]), r2y = _mm_set1_ps(VP.m[2][1]), r2z = _mm_set1_ps(VP.m[2][2]), r2w = _mm_set1_ps(VP.m[2][3]);
	const __m128 r3x = _mm_set1_ps(VP.m[3][0]), r3y = _mm_set1_ps(VP.m[3][1]), r3z = _mm_set1_ps(VP.m[3][2]), r3w = _mm_set1_ps(VP.m[3][3]);

	outX = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, r0x), _mm_mul_ps(Y, r0y)),
		_mm_add_ps(_mm_mul_ps(Z, r0z), _mm_mul_ps(ONE, r0w)));
	outY = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, r1x), _mm_mul_ps(Y, r1y)),
		_mm_add_ps(_mm_mul_ps(Z, r1z), _mm_mul_ps(ONE, r1w)));
	outZ = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, r2x), _mm_mul_ps(Y, r2y)),
		_mm_add_ps(_mm_mul_ps(Z, r2z), _mm_mul_ps(ONE, r2w)));
	outW = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, r3x), _mm_mul_ps(Y, r3y)),
		_mm_add_ps(_mm_mul_ps(Z, r3z), _mm_mul_ps(ONE, r3w)));
#   endif
}
#endif

float SFW::Graphics::ProjectQuadAreaPx2_SIMDOrScalar(
	const Vec3f quad[4],
	const Mat4f& VP, int vpW, int vpH,
	float* outMinX, float* outMinY, float* outMaxX, float* outMaxY,
	float* outDepthMeanNDC)
{
	float p[4][3] = {
		{quad[0].x, quad[0].y, quad[0].z},
		{quad[1].x, quad[1].y, quad[1].z},
		{quad[2].x, quad[2].y, quad[2].z},
		{quad[3].x, quad[3].y, quad[3].z},
	};

#if SFW_HAVE_SSE2 && defined(SFW_ROWMAJOR_MAT4F_HAS_M)
	__m128 cx, cy, cz, cw;
	TransformPoints4_SSE(p, VP, cx, cy, cz, cw);

	//「1頂点でもw<=0」ではなく「4頂点すべてw<=0」のときだけ捨てる
	const __m128 zero = _mm_set1_ps(0.0f);
	const __m128 wle = _mm_cmple_ps(cw, zero);
	const int mask = _mm_movemask_ps(wle);
	if (mask == 0xF) { // 全ビット立っている = 4頂点すべて w<=0
		if (outMinX) *outMinX = 0; if (outMinY) *outMinY = 0; if (outMaxX) *outMaxX = 0; if (outMaxY) *outMaxY = 0;
		if (outDepthMeanNDC) *outDepthMeanNDC = 1.0f;
		return 0.0f;
	}

	const __m128 invw = _mm_div_ps(_mm_set1_ps(1.0f), cw);
	const __m128 ndcX = _mm_mul_ps(cx, invw);
	const __m128 ndcY = _mm_mul_ps(cy, invw);
	const __m128 ndcZ = _mm_mul_ps(cz, invw);

	const __m128 half = _mm_set1_ps(0.5f);
	const __m128 one = _mm_set1_ps(1.0f);
	__m128 sx = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(ndcX, half), half), _mm_set1_ps((float)vpW));
	__m128 sy = _mm_mul_ps(_mm_sub_ps(one, _mm_add_ps(_mm_mul_ps(ndcY, half), half)), _mm_set1_ps((float)vpH));

	float minx = hmin4(sx), miny = hmin4(sy);
	float maxx = hmax4(sx), maxy = hmax4(sy);
	float w = (std::max)(0.0f, maxx - minx);
	float h = (std::max)(0.0f, maxy - miny);

	if (outMinX) *outMinX = minx; if (outMinY) *outMinY = miny;
	if (outMaxX) *outMaxX = maxx; if (outMaxY) *outMaxY = maxy;
	if (outDepthMeanNDC) {
		alignas(16) float z[4];
		_mm_store_ps(z, ndcZ);
		*outDepthMeanNDC = 0.25f * (z[0] + z[1] + z[2] + z[3]);
	}
	return w * h;
#else
	// scalar fallback
	float minx = +1e9f, miny = +1e9f, maxx = -1e9f, maxy = -1e9f;
	float zsum = 0.0f; int zn = 0;
	for (int i = 0; i < 4; ++i) {
		const Vec4f c = MulPointClip_ByVP(VP, Vec4f(p[i][0], p[i][1], p[i][2], 1.0f));
		if (c.w <= 0.0f) {
			if (outMinX) *outMinX = 0; if (outMinY) *outMinY = 0; if (outMaxX) *outMaxX = 0; if (outMaxY) *outMaxY = 0;
			if (outDepthMeanNDC) *outDepthMeanNDC = 1.0f;
			return 0.0f;
		}
		float invw = 1.0f / c.w;
		float ndcX = c.x * invw;
		float ndcY = c.y * invw;
		float ndcZ = c.z * invw;
		float sx = (ndcX * 0.5f + 0.5f) * vpW;
		float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * vpH;
		minx = (std::min)(minx, sx); miny = (std::min)(miny, sy);
		maxx = (std::max)(maxx, sx); maxy = (std::max)(maxy, sy);
		zsum += ndcZ; ++zn;
	}
	float w = (std::max)(0.0f, maxx - minx);
	float h = (std::max)(0.0f, maxy - miny);
	if (outMinX) *outMinX = minx; if (outMinY) *outMinY = miny;
	if (outMaxX) *outMaxX = maxx; if (outMaxY) *outMaxY = maxy;
	if (outDepthMeanNDC) *outDepthMeanNDC = (zn > 0) ? (zsum / zn) : 1.0f;
	return w * h;
#endif
}

// -----------------------------------------------------------------------------
// (D) LOD policy & selection
// -----------------------------------------------------------------------------
OccluderPolicy SFW::Graphics::GetPolicy(OccluderLOD lod) {
	static constexpr OccluderPolicy policy[] = {
		 { 10.f, 100.f, 3, 30000, 64, 1.0f },
		 { 14.f, 196.f, 2, 20000, 64, 1.3f },
		 { 18.f, 324.f, 1, 10000, 64, 1.6f }
	};

	return policy[(int)lod];
}

static inline int TileIdFromScreenAABB_Local(float minx, float miny, float maxx, float maxy,
	int vpW, int vpH, int tilePx, int& tilesX) {
	tilesX = (vpW + tilePx - 1) / tilePx;

	// 画面と全く交差しないなら -1
	if (maxx <= 0.0f || maxy <= 0.0f || minx >= (float)vpW || miny >= (float)vpH)
		return -1;

	// 画面内にクリップ
	float cminx = (std::max)(0.0f, minx);
	float cminy = (std::max)(0.0f, miny);
	float cmaxx = (std::min)((float)vpW, maxx);
	float cmaxy = (std::min)((float)vpH, maxy);

	// クリップ後中心でタイル決定（必ず画面内になる）
	float cx = 0.5f * (cminx + cmaxx);
	float cy = 0.5f * (cminy + cmaxy);

	int x = (int)cx / tilePx;
	int y = (int)cy / tilePx;

	// 念のためクランプ（境界ちょうどのときの安全）
	x = (std::min)((std::max)(0, x), tilesX - 1);
	int tilesY = (vpH + tilePx - 1) / tilePx;
	y = (std::min)((std::max)(0, y), tilesY - 1);

	return y * tilesX + x;
}

int SFW::Graphics::SelectOccluderQuads_SIMD(
	const std::vector<AABB3f>& aabbs,
	const Vec3f& camPos,
	const Mat4f& VP,
	int vpW, int vpH,
	OccluderLOD lod,
	std::vector<QuadCandidate>& out)
{
	out.clear();
	out.reserve(aabbs.size());
	const OccluderViewport vp{ vpW, vpH, 1.0f };
	const OccluderPolicy P = GetPolicy(lod);

	for (const auto& b : aabbs) {
		if (EstimateMaxScreenDiameterPx(b, camPos, vp) < P.minEdgePx) continue;

		AABBFrontFaceQuad q;
		if (!ComputeFrontFaceQuad(b, camPos, q)) continue;

		float minx, miny, maxx, maxy, zmean;
		float area = ProjectQuadAreaPx2_SIMDOrScalar(q.v, VP, vpW, vpH, &minx, &miny, &maxx, &maxy, &zmean);
		if (area < P.minAreaPx2) continue;

		int tilesX = 0;
		int tileId = TileIdFromScreenAABB_Local(minx, miny, maxx, maxy, vpW, vpH, P.tileSizePx, tilesX);
		if (tileId < 0) continue;

		float depth = (zmean + 1.0f) * 0.5f + 1e-3f;
		float score = area / std::pow(depth, P.scoreDepthAlpha);
		QuadCandidate qc{};
		qc.quad = q;
		qc.areaPx2 = area;
		qc.score = score;
		qc.tileId = tileId;
		qc.clip[0] = MulPointClip_ByVP(VP, Vec4f(q.v[0].x, q.v[0].y, q.v[0].z, 1.0f));
		qc.clip[1] = MulPointClip_ByVP(VP, Vec4f(q.v[1].x, q.v[1].y, q.v[1].z, 1.0f));
		qc.clip[2] = MulPointClip_ByVP(VP, Vec4f(q.v[2].x, q.v[2].y, q.v[2].z, 1.0f));
		qc.clip[3] = MulPointClip_ByVP(VP, Vec4f(q.v[3].x, q.v[3].y, q.v[3].z, 1.0f));
		out.push_back(qc);
	}

	std::unordered_map<int, std::vector<size_t>> perTile;
	perTile.reserve(out.size());
	for (size_t i = 0; i < out.size(); ++i) perTile[out[i].tileId].push_back(i);

	std::vector<QuadCandidate> filtered;
	filtered.reserve(out.size());
	for (auto& kv : perTile) {
		auto& idxs = kv.second;
		int take = (std::min)((int)idxs.size(), P.tileK);
		std::partial_sort(idxs.begin(), idxs.begin() + take, idxs.end(),
			[&](size_t a, size_t b) { return out[a].score > out[b].score; });
		for (int i = 0; i < take; ++i) filtered.push_back(out[idxs[i]]);
	}

	std::sort(filtered.begin(), filtered.end(),
		[](const QuadCandidate& a, const QuadCandidate& b) { return a.score > b.score; });
	int maxQuads = (std::max)(0, P.globalTriBudget / 2);
	if ((int)filtered.size() > maxQuads) filtered.resize(maxQuads);

	out.swap(filtered);
	return (int)out.size();
}

// ---- AVX2 batch projection of 2 quads (8 points) ----
#if SFW_HAVE_AVX2 && defined(SFW_ROWMAJOR_MAT4F_HAS_M)
struct TwoQuadProjectOut {
	float minxA, minyA, maxxA, maxyA, areaA, zmeanA;
	float minxB, minyB, maxxB, maxyB, areaB, zmeanB;
};

static inline void TransformPoints8_AVX2(const float p[8][3], const Mat4f& VP,
	__m256& outX, __m256& outY, __m256& outZ, __m256& outW)
{
	const __m256 X = _mm256_set_ps(p[7][0], p[6][0], p[5][0], p[4][0], p[3][0], p[2][0], p[1][0], p[0][0]);
	const __m256 Y = _mm256_set_ps(p[7][1], p[6][1], p[5][1], p[4][1], p[3][1], p[2][1], p[1][1], p[0][1]);
	const __m256 Z = _mm256_set_ps(p[7][2], p[6][2], p[5][2], p[4][2], p[3][2], p[2][2], p[1][2], p[0][2]);
	const __m256 ONE = _mm256_set1_ps(1.0f);

#   if SFW_MATH_ROWVEC
	// v*M：列を使う
	const __m256 c0x = _mm256_set1_ps(VP.m[0][0]), c1x = _mm256_set1_ps(VP.m[1][0]), c2x = _mm256_set1_ps(VP.m[2][0]), c3x = _mm256_set1_ps(VP.m[3][0]);
	const __m256 c0y = _mm256_set1_ps(VP.m[0][1]), c1y = _mm256_set1_ps(VP.m[1][1]), c2y = _mm256_set1_ps(VP.m[2][1]), c3y = _mm256_set1_ps(VP.m[3][1]);
	const __m256 c0z = _mm256_set1_ps(VP.m[0][2]), c1z = _mm256_set1_ps(VP.m[1][2]), c2z = _mm256_set1_ps(VP.m[2][2]), c3z = _mm256_set1_ps(VP.m[3][2]);
	const __m256 c0w = _mm256_set1_ps(VP.m[0][3]), c1w = _mm256_set1_ps(VP.m[1][3]), c2w = _mm256_set1_ps(VP.m[2][3]), c3w = _mm256_set1_ps(VP.m[3][3]);

	outX = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, c0x), _mm256_mul_ps(Y, c1x)),
		_mm256_add_ps(_mm256_mul_ps(Z, c2x), _mm256_mul_ps(ONE, c3x)));
	outY = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, c0y), _mm256_mul_ps(Y, c1y)),
		_mm256_add_ps(_mm256_mul_ps(Z, c2y), _mm256_mul_ps(ONE, c3y)));
	outZ = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, c0z), _mm256_mul_ps(Y, c1z)),
		_mm256_add_ps(_mm256_mul_ps(Z, c2z), _mm256_mul_ps(ONE, c3z)));
	outW = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, c0w), _mm256_mul_ps(Y, c1w)),
		_mm256_add_ps(_mm256_mul_ps(Z, c2w), _mm256_mul_ps(ONE, c3w)));
#   else
	// M*v：行を使う
	const __m256 r0x = _mm256_set1_ps(VP.m[0][0]), r0y = _mm256_set1_ps(VP.m[0][1]), r0z = _mm256_set1_ps(VP.m[0][2]), r0w = _mm256_set1_ps(VP.m[0][3]);
	const __m256 r1x = _mm256_set1_ps(VP.m[1][0]), r1y = _mm256_set1_ps(VP.m[1][1]), r1z = _mm256_set1_ps(VP.m[1][2]), r1w = _mm256_set1_ps(VP.m[1][3]);
	const __m256 r2x = _mm256_set1_ps(VP.m[2][0]), r2y = _mm256_set1_ps(VP.m[2][1]), r2z = _mm256_set1_ps(VP.m[2][2]), r2w = _mm256_set1_ps(VP.m[2][3]);
	const __m256 r3x = _mm256_set1_ps(VP.m[3][0]), r3y = _mm256_set1_ps(VP.m[3][1]), r3z = _mm256_set1_ps(VP.m[3][2]), r3w = _mm256_set1_ps(VP.m[3][3]);

	outX = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, r0x), _mm256_mul_ps(Y, r0y)),
		_mm256_add_ps(_mm256_mul_ps(Z, r0z), _mm256_mul_ps(ONE, r0w)));
	outY = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, r1x), _mm256_mul_ps(Y, r1y)),
		_mm256_add_ps(_mm256_mul_ps(Z, r1z), _mm256_mul_ps(ONE, r1w)));
	outZ = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, r2x), _mm256_mul_ps(Y, r2y)),
		_mm256_add_ps(_mm256_mul_ps(Z, r2z), _mm256_mul_ps(ONE, r2w)));
	outW = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, r3x), _mm256_mul_ps(Y, r3y)),
		_mm256_add_ps(_mm256_mul_ps(Z, r3z), _mm256_mul_ps(ONE, r3w)));
#   endif
}

static inline float hmin4_128(__m128 v) {
	__m128 t = _mm_min_ps(v, _mm_movehl_ps(v, v));
	t = _mm_min_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 2, 0, 0)));
	return _mm_cvtss_f32(t);
}
static inline float hmax4_128(__m128 v) {
	__m128 t = _mm_max_ps(v, _mm_movehl_ps(v, v));
	t = _mm_max_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 2, 0, 0)));
	return _mm_cvtss_f32(t);
}

static inline TwoQuadProjectOut ProjectTwoQuadsAreaPx2_AVX2(
	const Vec3f quadA[4], const Vec3f quadB[4],
	const Mat4f& VP, int vpW, int vpH)
{
	float p[8][3] = {
		{quadA[0].x, quadA[0].y, quadA[0].z},
		{quadA[1].x, quadA[1].y, quadA[1].z},
		{quadA[2].x, quadA[2].y, quadA[2].z},
		{quadA[3].x, quadA[3].y, quadA[3].z},
		{quadB[0].x, quadB[0].y, quadB[0].z},
		{quadB[1].x, quadB[1].y, quadB[1].z},
		{quadB[2].x, quadB[2].y, quadB[2].z},
		{quadB[3].x, quadB[3].y, quadB[3].z},
	};

	__m256 cx, cy, cz, cw;
	TransformPoints8_AVX2(p, VP, cx, cy, cz, cw);

	const __m256 zero = _mm256_set1_ps(0.0f);
	__m256 wle = _mm256_cmp_ps(cw, zero, _CMP_LE_OQ);
	int m = _mm256_movemask_ps(wle);

	//「1頂点でもw<=0」ではなく「4頂点すべてw<=0」のときだけreject
	const int mA = (m & 0x0F);
	const int mB = ((m >> 4) & 0x0F);
	bool rejA = (mA == 0x0F);
	bool rejB = (mB == 0x0F);

	__m256 invw = _mm256_div_ps(_mm256_set1_ps(1.0f), cw);
	__m256 ndcX = _mm256_mul_ps(cx, invw);
	__m256 ndcY = _mm256_mul_ps(cy, invw);
	__m256 ndcZ = _mm256_mul_ps(cz, invw);

	const __m256 half = _mm256_set1_ps(0.5f);
	const __m256 one = _mm256_set1_ps(1.0f);
	__m256 sx = _mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(ndcX, half), half), _mm256_set1_ps((float)vpW));
	__m256 sy = _mm256_mul_ps(_mm256_sub_ps(one, _mm256_add_ps(_mm256_mul_ps(ndcY, half), half)), _mm256_set1_ps((float)vpH));

	__m128 sxA = _mm256_castps256_ps128(sx);
	__m128 sxB = _mm256_extractf128_ps(sx, 1);
	__m128 syA = _mm256_castps256_ps128(sy);
	__m128 syB = _mm256_extractf128_ps(sy, 1);
	__m128 zA = _mm256_castps256_ps128(ndcZ);
	__m128 zB = _mm256_extractf128_ps(ndcZ, 1);

	float minxA = hmin4_128(sxA), maxxA = hmax4_128(sxA);
	float minyA = hmin4_128(syA), maxyA = hmax4_128(syA);
	float minxB = hmin4_128(sxB), maxxB = hmax4_128(sxB);
	float minyB = hmin4_128(syB), maxyB = hmax4_128(syB);

	float wA = (std::max)(0.0f, maxxA - minxA);
	float hA = (std::max)(0.0f, maxyA - minyA);
	float wB = (std::max)(0.0f, maxxB - minxB);
	float hB = (std::max)(0.0f, maxyB - minyB);

	alignas(16) float zbufA[4], zbufB[4];
	_mm_store_ps(zbufA, zA);
	_mm_store_ps(zbufB, zB);
	float zmeanA = 0.25f * (zbufA[0] + zbufA[1] + zbufA[2] + zbufA[3]);
	float zmeanB = 0.25f * (zbufB[0] + zbufB[1] + zbufB[2] + zbufB[3]);

	TwoQuadProjectOut out{};
	out.minxA = minxA; out.maxxA = maxxA; out.minyA = minyA; out.maxyA = maxyA;
	out.minxB = minxB; out.maxxB = maxxB; out.minyB = minyB; out.maxyB = maxyB;
	out.areaA = rejA ? 0.0f : (wA * hA);
	out.areaB = rejB ? 0.0f : (wB * hB);
	out.zmeanA = zmeanA; out.zmeanB = zmeanB;
	return out;
}
#endif

int SFW::Graphics::SelectOccluderQuads_AVX2(
	const Math::AABB3f* aabbs,
	const size_t aabbCount,
	const Vec3f& camPos,
	const Mat4f& VP,
	const OccluderViewport& vp,
	OccluderLOD lod,
	std::vector<QuadCandidate>& out,
	AABBQuadAxisBit axisBit)
{
#if SFW_HAVE_AVX2 && defined(SFW_ROWMAJOR_MAT4F_HAS_M)
	out.clear();
	out.reserve(aabbCount);

	const OccluderPolicy P = GetPolicy(lod);

	std::vector<AABBFrontFaceQuad> quads; quads.reserve(aabbCount);

	for (int i = 0; i < aabbCount; ++i) {
		const auto& b = aabbs[i];
		if (EstimateMaxScreenDiameterPx(b, camPos, vp) < P.minEdgePx) {
			continue;
		}
		AABBFrontFaceQuad q;
		if (!ComputeFrontFaceQuad(b, camPos, q, axisBit)) {
			continue;
		}
		quads.push_back(q);
	}

	for (size_t i = 0; i < quads.size(); i += 2) {
		if (i + 1 < quads.size()) {
			// Direction B: do VP transform once here (8 points), and reuse it for
			//  - clip coords (stored to QuadCandidate)
			//  - screen AABB + area
			//  - mean NDC depth
			float p8[8][3] = {
				{quads[i + 0].v[0].x, quads[i + 0].v[0].y, quads[i + 0].v[0].z},

				{quads[i + 0].v[1].x, quads[i + 0].v[1].y, quads[i + 0].v[1].z},

				{quads[i + 0].v[2].x, quads[i + 0].v[2].y, quads[i + 0].v[2].z},

				{quads[i + 0].v[3].x, quads[i + 0].v[3].y, quads[i + 0].v[3].z},

				{quads[i + 1].v[0].x, quads[i + 1].v[0].y, quads[i + 1].v[0].z},

				{quads[i + 1].v[1].x, quads[i + 1].v[1].y, quads[i + 1].v[1].z},

				{quads[i + 1].v[2].x, quads[i + 1].v[2].y, quads[i + 1].v[2].z},

				{quads[i + 1].v[3].x, quads[i + 1].v[3].y, quads[i + 1].v[3].z},
			};

			__m256 cx, cy, cz, cw;

			TransformPoints8_AVX2(p8, VP, cx, cy, cz, cw);

			// Reject only if all 4 vertices are w<=0 (per-quad)

			const __m256 zero = _mm256_set1_ps(0.0f);

			const __m256 wle = _mm256_cmp_ps(cw, zero, _CMP_LE_OQ);

			const int m = _mm256_movemask_ps(wle);

			const int mA = (m & 0x0F);

			const int mB = ((m >> 4) & 0x0F);

			const bool rejA = (mA == 0x0F);

			const bool rejB = (mB == 0x0F);

			// Safety: avoid INF/NaN when some vertices have w<=0 (we still keep original clip.w)

			const __m256 eps = _mm256_set1_ps(1e-6f);

			__m256 cwSafe = _mm256_max_ps(cw, eps);

			__m256 invw = _mm256_div_ps(_mm256_set1_ps(1.0f), cwSafe);

			__m256 ndcX = _mm256_mul_ps(cx, invw);

			__m256 ndcY = _mm256_mul_ps(cy, invw);

			__m256 ndcZ = _mm256_mul_ps(cz, invw);

			const __m256 half = _mm256_set1_ps(0.5f);

			const __m256 one = _mm256_set1_ps(1.0f);

			__m256 sx = _mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(ndcX, half), half), _mm256_set1_ps((float)vp.width));

			__m256 sy = _mm256_mul_ps(_mm256_sub_ps(one, _mm256_add_ps(_mm256_mul_ps(ndcY, half), half)), _mm256_set1_ps((float)vp.height));

			__m128 sxA = _mm256_castps256_ps128(sx);

			__m128 sxB = _mm256_extractf128_ps(sx, 1);

			__m128 syA = _mm256_castps256_ps128(sy);

			__m128 syB = _mm256_extractf128_ps(sy, 1);

			__m128 zA = _mm256_castps256_ps128(ndcZ);

			__m128 zB = _mm256_extractf128_ps(ndcZ, 1);

			float minxA = hmin4_128(sxA), maxxA = hmax4_128(sxA);

			float minyA = hmin4_128(syA), maxyA = hmax4_128(syA);

			float minxB = hmin4_128(sxB), maxxB = hmax4_128(sxB);

			float minyB = hmin4_128(syB), maxyB = hmax4_128(syB);

			float wA = (std::max)(0.0f, maxxA - minxA);

			float hA = (std::max)(0.0f, maxyA - minyA);

			float wB = (std::max)(0.0f, maxxB - minxB);

			float hB = (std::max)(0.0f, maxyB - minyB);

			float areaA = rejA ? 0.0f : (wA * hA);

			float areaB = rejB ? 0.0f : (wB * hB);

			alignas(16) float zbufA[4], zbufB[4];

			_mm_store_ps(zbufA, zA);

			_mm_store_ps(zbufB, zB);

			float zmeanA = 0.25f * (zbufA[0] + zbufA[1] + zbufA[2] + zbufA[3]);

			float zmeanB = 0.25f * (zbufB[0] + zbufB[1] + zbufB[2] + zbufB[3]);

			// Store clip (original cx/cy/cz/cw)

			alignas(32) float xbuf[8], ybuf[8], zbuf[8], wbuf[8];

			_mm256_store_ps(xbuf, cx);

			_mm256_store_ps(ybuf, cy);

			_mm256_store_ps(zbuf, cz);

			_mm256_store_ps(wbuf, cw);

			if (areaA >= P.minAreaPx2) {
				int tilesX = 0;

				int tileId = TileIdFromScreenAABB_Local(minxA, minyA, maxxA, maxyA, vp.width, vp.height, P.tileSizePx, tilesX);

				if (tileId >= 0) {
					float depth = (zmeanA + 1.0f) * 0.5f + 1e-3f;

					float score = areaA / std::pow(depth, P.scoreDepthAlpha);

					QuadCandidate qc{};

					qc.quad = quads[i + 0];

					qc.areaPx2 = areaA;

					qc.score = score;

					qc.tileId = tileId;

					qc.clip[0] = Vec4f(xbuf[0], ybuf[0], zbuf[0], wbuf[0]);

					qc.clip[1] = Vec4f(xbuf[1], ybuf[1], zbuf[1], wbuf[1]);

					qc.clip[2] = Vec4f(xbuf[2], ybuf[2], zbuf[2], wbuf[2]);

					qc.clip[3] = Vec4f(xbuf[3], ybuf[3], zbuf[3], wbuf[3]);

					out.push_back(qc);
				}
			}

			if (areaB >= P.minAreaPx2) {
				int tilesX = 0;

				int tileId = TileIdFromScreenAABB_Local(minxB, minyB, maxxB, maxyB, vp.width, vp.height, P.tileSizePx, tilesX);

				if (tileId >= 0) {
					float depth = (zmeanB + 1.0f) * 0.5f + 1e-3f;

					float score = areaB / std::pow(depth, P.scoreDepthAlpha);

					QuadCandidate qc{};

					qc.quad = quads[i + 1];

					qc.areaPx2 = areaB;

					qc.score = score;

					qc.tileId = tileId;

					qc.clip[0] = Vec4f(xbuf[4], ybuf[4], zbuf[4], wbuf[4]);

					qc.clip[1] = Vec4f(xbuf[5], ybuf[5], zbuf[5], wbuf[5]);

					qc.clip[2] = Vec4f(xbuf[6], ybuf[6], zbuf[6], wbuf[6]);

					qc.clip[3] = Vec4f(xbuf[7], ybuf[7], zbuf[7], wbuf[7]);

					out.push_back(qc);
				}
			}
		}
		else {
			float minx, miny, maxx, maxy, zmean;
			float area = ProjectQuadAreaPx2_SIMDOrScalar(quads[i].v, VP, vp.width, vp.height, &minx, &miny, &maxx, &maxy, &zmean);
			if (area >= P.minAreaPx2) {
				int tilesX = 0;
				int tileId = TileIdFromScreenAABB_Local(minx, miny, maxx, maxy, vp.width, vp.height, P.tileSizePx, tilesX);
				if (tileId >= 0) {
					float depth = (zmean + 1.0f) * 0.5f + 1e-3f;
					float score = area / std::pow(depth, P.scoreDepthAlpha);
					QuadCandidate qc{};
					qc.quad = quads[i];
					qc.areaPx2 = area;
					qc.score = score;
					qc.tileId = tileId;
					qc.clip[0] = MulPointClip_ByVP(VP, Vec4f(quads[i].v[0].x, quads[i].v[0].y, quads[i].v[0].z, 1.0f));
					qc.clip[1] = MulPointClip_ByVP(VP, Vec4f(quads[i].v[1].x, quads[i].v[1].y, quads[i].v[1].z, 1.0f));
					qc.clip[2] = MulPointClip_ByVP(VP, Vec4f(quads[i].v[2].x, quads[i].v[2].y, quads[i].v[2].z, 1.0f));
					qc.clip[3] = MulPointClip_ByVP(VP, Vec4f(quads[i].v[3].x, quads[i].v[3].y, quads[i].v[3].z, 1.0f));
					out.push_back(qc);
				}
			}
		}
	}

	std::unordered_map<int, std::vector<size_t>> perTile;
	perTile.reserve(out.size());
	for (size_t i = 0; i < out.size(); ++i) perTile[out[i].tileId].push_back(i);

	std::vector<QuadCandidate> filtered;
	filtered.reserve(out.size());
	for (auto& kv : perTile) {
		auto& idxs = kv.second;
		int take = (std::min)((int)idxs.size(), P.tileK);
		std::partial_sort(idxs.begin(), idxs.begin() + take, idxs.end(),
			[&](size_t a, size_t b) { return out[a].score > out[b].score; });
		for (int i = 0; i < take; ++i) filtered.push_back(out[idxs[i]]);
	}

	std::sort(filtered.begin(), filtered.end(),
		[](const QuadCandidate& a, const QuadCandidate& b) { return a.score > b.score; });
	int maxQuads = (std::max)(0, P.globalTriBudget / 2);
	if ((int)filtered.size() > maxQuads) filtered.resize(maxQuads);

	out.swap(filtered);
	return (int)out.size();
#else
	// Fallback seamlessly to the SSE/scalar selector
	return SelectOccluderQuads_SIMD(std::vector<AABB3f>(aabbs, aabbs + aabbCount), camPos, VP, vp.width, vp.height, lod, out);
#endif
}

// -----------------------------------------------------------------------------
// (E) LOD helpers
// -----------------------------------------------------------------------------
float SFW::Graphics::OccluderBiasFromRenderLod(int visLod) {
	if (visLod <= 0) return 0.0f;
	if (visLod == 1) return +0.2f;
	return +0.4f;
}

float SFW::Graphics::ScreenCoverageFromRectPx(float minx, float miny, float maxx, float maxy,
	float vpW, float vpH)
{
	float w = (std::max)(0.f, maxx - minx);
	float h = (std::max)(0.f, maxy - miny);
	float area = w * h;
	float full = float(vpW) * float(vpH);
	return (std::min)(area / (std::max)(1.f, full), 1.0f);
}

float SFW::Graphics::ComputeNDCAreaFrec(float minx, float miny, float maxx, float maxy)
{
	// 画面ボックス [-1,1] と交差
	const float ix0 = (std::max)(minx, -1.0f);
	const float iy0 = (std::max)(miny, -1.0f);
	const float ix1 = (std::min)(maxx, 1.0f);
	const float iy1 = (std::min)(maxy, 1.0f);
	const float w = (ix1 > ix0) ? (ix1 - ix0) : 0.0f;
	const float h = (iy1 > iy0) ? (iy1 - iy0) : 0.0f;
	// 画面全体の NDC 面積は 4（幅2×高さ2）
	return (w * h) * 0.25f;
}

OccluderLOD SFW::Graphics::DecideOccluderLOD_FromArea(float areaPx2) {
	if (areaPx2 >= 400.0f) return OccluderLOD::Near; // >=20x20px
	if (areaPx2 >= 196.0f) return OccluderLOD::Mid;  // >=14x14px
	return OccluderLOD::Far;
}

void SFW::Graphics::CoarseSphereVisible_AVX2(const SoAPosRad& s, const ViewProjParams& vp, std::vector<uint32_t>& outIndices)
{
	outIndices.clear();
	outIndices.reserve(s.count);

	const __m256 v30 = _mm256_set1_ps(vp.v30);
	const __m256 v31 = _mm256_set1_ps(vp.v31);
	const __m256 v32 = _mm256_set1_ps(vp.v32);
	const __m256 v33 = _mm256_set1_ps(vp.v33);

	const __m256 P00 = _mm256_set1_ps(vp.P00);
	const __m256 P11 = _mm256_set1_ps(vp.P11);
	const __m256 zN = _mm256_set1_ps(vp.zNear);
	const __m256 zF = _mm256_set1_ps(vp.zFar);
	const __m256 eps = _mm256_set1_ps(vp.epsNdc);

	const bool hasR = (s.pr != nullptr);
	const __m256 rScalar = _mm256_set1_ps(hasR ? 0.0f : /*固定半径*/ 1.0f);

	const uint32_t N = s.count;
	uint32_t i = 0;

	alignas(32) uint32_t idxBuf[8];

	for (; i + 8 <= N; i += 8) {
		__m256 x = _mm256_loadu_ps(s.px + i);
		__m256 y = _mm256_loadu_ps(s.py + i);
		__m256 z = _mm256_loadu_ps(s.pz + i);
		__m256 r = hasR ? _mm256_loadu_ps(s.pr + i) : rScalar;

		// cvz = v30*x + v31*y + v32*z + v33
		__m256 cvz = _mm256_fmadd_ps(v30, x,
			_mm256_fmadd_ps(v31, y,
				_mm256_fmadd_ps(v32, z, v33)));

		// 条件1: cvz > 0
		__m256 m1 = _mm256_cmp_ps(cvz, _mm256_set1_ps(0.0f), _CMP_GT_OQ);

		// 条件2: cvz + r > zNear
		__m256 c2 = _mm256_cmp_ps(_mm256_add_ps(cvz, r), zN, _CMP_GT_OQ);

		// 条件3: cvz - r < zFar
		__m256 c3 = _mm256_cmp_ps(_mm256_sub_ps(cvz, r), zF, _CMP_LT_OQ);

		// 画面半径: max(r*P00/cvz, r*P11/cvz) >= eps
		// 安全のためcvz>0のみでdivを評価したいが、ここではそのまま評価してマスクで落とす
		__m256 rx = _mm256_div_ps(_mm256_mul_ps(r, P00), cvz);
		__m256 ry = _mm256_div_ps(_mm256_mul_ps(r, P11), cvz);
		__m256 r_ndc = _mm256_max_ps(_mm256_andnot_ps(_mm256_set1_ps(-0.0f), rx), // abs
			_mm256_andnot_ps(_mm256_set1_ps(-0.0f), ry));
		__m256 c4 = _mm256_cmp_ps(r_ndc, eps, _CMP_GE_OQ);

		// AND
		__m256 m = _mm256_and_ps(_mm256_and_ps(m1, c2), _mm256_and_ps(c3, c4));

		// マスク→生存インデックスをpush_back
		int mask = _mm256_movemask_ps(m); // 下位8bitが有効
		if (mask == 0) continue;

		// 8連番を準備
		idxBuf[0] = i + 0; idxBuf[1] = i + 1; idxBuf[2] = i + 2; idxBuf[3] = i + 3;
		idxBuf[4] = i + 4; idxBuf[5] = i + 5; idxBuf[6] = i + 6; idxBuf[7] = i + 7;

		// compress-store（AVX2にはないので分岐で詰める）
		// 8要素だけの分岐は軽い
		for (int k = 0; k < 8; ++k) {
			if (mask & (1 << k)) outIndices.push_back(idxBuf[k]);
		}
	}

	// 端数
	for (; i < N; ++i) {
		float x = s.px[i], y = s.py[i], z = s.pz[i];
		float r = hasR ? s.pr[i] : 1.0f;

		float cvz = vp.v30 * x + vp.v31 * y + vp.v32 * z + vp.v33;
		if (!(cvz > 0.0f)) continue;
		if (!((cvz + r) > vp.zNear)) continue;
		if (!((cvz - r) < vp.zFar))  continue;

		float rx = r * vp.P00 / cvz;
		float ry = r * vp.P11 / cvz;
		float rndc = (std::abs(rx) > std::abs(ry)) ? std::abs(rx) : std::abs(ry);
		if (rndc >= vp.epsNdc) outIndices.push_back(i);
	}
}