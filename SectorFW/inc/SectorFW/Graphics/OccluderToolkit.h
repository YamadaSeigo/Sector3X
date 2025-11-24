
// OccluderToolkit.hpp
// -----------------------------------------------------------------------------
// Public API for occluder generation & selection (DX11/MOC friendly)
//  - melt integration is hidden in .cpp (this header DOES NOT include melt.h)
//  - AABB -> front-face quad selection
//  - SIMD/AVX2 accelerated projection & selection (implemented in .cpp)
//  - Occluder LOD helpers (Near/Mid/Far)
//
// Usage notes:
//   * Define SFW_ROWMAJOR_MAT4F_HAS_M if Math::Matrix4x4f exposes row-major storage m[4][4].
//     SIMD/AVX2 paths require this. Without it, scalar paths are used.
//   * To enable AVX2 in MSVC: /arch:AVX2, Clang/GCC: -mavx2
//   * To enable melt: build OccluderToolkit.cpp with melt.c and melt.h available.
//     You may define HAVE_MELT=1 manually or rely on auto-detection in the .cpp.
//   - Define SFW_MATH_ROWVEC=1 if your math uses row-vector (v*M).
//   The toolkit will internally transpose VP so you can pass (View*Proj) as-is.
//
//  Original author: ChatGPT-5
// -----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <immintrin.h>

#include "../Math/Vector.hpp"   // SectorFW::Math::{Math::Vec3f, Math::Vec4f}
#include "../Math/Matrix.hpp"   // SectorFW::Math::Math::Matrix4x4f (row-major preferred)
#include "../Math/AABB.hpp"     // SectorFW::Math::Math::AABB3f { lb, ub }

#define SFW_ROWMAJOR_MAT4F_HAS_M

#ifndef SFW_MATH_ROWVEC
#define SFW_MATH_ROWVEC 0
#endif

namespace SFW {
    namespace Graphics {

        // ----- melt result status (melt.h is hidden in .cpp) -----
        enum class MeltBuildStatus : uint32_t {
            UsedMelt = 0,
            FallbackWhole = 1,
            Failed = 2
        };

        // Generate occluder AABBs using melt (if available) or fallback to whole AABB.
        // meltResolution: nominal grid resolution along max extent (e.g. 64). <=0 uses default.
        // meltFillPct: 0..1 density target (typ. 0.25~0.6).
        MeltBuildStatus GenerateOccluderAABBs_MaybeWithMelt(
            const std::vector<Math::Vec3f>& positions,
            const std::vector<uint32_t>& indices,
            int   meltResolution,
            float meltFillPct,
            std::vector<Math::AABB3f>& outAABBs);

        // ----- Front face quad from AABB (O(1)) -----
        struct AABBFrontFaceQuad {
            Math::Vec3f v[4];      // CCW as seen from camera
            Math::Vec3f normal;    // +-X/Y/Z
            int  axis = -1;  // 0:X,1:Y,2:Z
            bool positive = true;
        };

        bool ComputeFrontFaceQuad(const Math::AABB3f& b, const Math::Vec3f& camPos, AABBFrontFaceQuad& out);
        void QuadToTrianglesCCW(uint16_t outIdx[6]);

        // ----- Screen-space helpers -----
        struct OccluderViewport {
            int   width = 0;
            int   height = 0;
            float fovY = 1.0f; // radians (coarse estimate only)
        };

        float EstimateMaxScreenDiameterPx(const Math::AABB3f& b, const Math::Vec3f& camPos, const OccluderViewport& vp);

        // Project a quad -> screen AABB area (px^2), optionally returns screen AABB and mean NDC depth.
        // SIMD/AVX2 acceleration is implemented in .cpp when possible.
        float ProjectQuadAreaPx2_SIMDOrScalar(
            const Math::Vec3f quad[4],
            const Math::Matrix4x4f& VP, int vpW, int vpH,
            float* outMinX = nullptr, float* outMinY = nullptr, float* outMaxX = nullptr, float* outMaxY = nullptr,
            float* outDepthMeanNDC = nullptr);

        // ----- LOD policy -----
        enum class OccluderLOD { Near, Mid, Far };

        struct OccluderPolicy {
            float minEdgePx;       // coarse reject using bounding sphere diameter estimate
            float minAreaPx2;      // min projected area for acceptance
            int   tileK;           // per-tile top-K
            int   globalTriBudget; // global triangle budget (each quad = 2 tris)
            int   tileSizePx;      // tile size for per-tile selection
            float scoreDepthAlpha; // area / depth^alpha
        };

        OccluderPolicy GetPolicy(OccluderLOD lod);

        struct QuadCandidate {
            AABBFrontFaceQuad quad;
            float areaPx2 = 0.0f;
            float score = 0.0f;
            int   tileId = -1;
        };

        // Select occluder quads (SSE2/scalar path).
        int SelectOccluderQuads_SIMD(
            const std::vector<Math::AABB3f>& aabbs,
            const Math::Vec3f& camPos,
            const Math::Matrix4x4f& VP,
            int vpW, int vpH,
            OccluderLOD lod,
            std::vector<QuadCandidate>& out);

        // AVX2 path (2 quads at a time). If AVX2/row-major is unavailable, this will internally fall back to the SIMD/scalar path.
        int SelectOccluderQuads_AVX2(
            const Math::AABB3f* aabbs,
			const size_t aabbCount,
            const Math::Vec3f& camPos,
            const Math::Matrix4x4f& VP,
            const OccluderViewport& vp,
            OccluderLOD lod,
            std::vector<QuadCandidate>& out);

        // ----- Reuse render LOD thresholds for Occluder LOD -----
        // Generic LOD selector with hysteresis & bias.
        // TTh must have: std::array<float,N> T; float hysteresisUp; float hysteresisDown;
        template<class TTh>
        int SelectLodGeneric(float s, const TTh& th, int lodCount, int prevLod, float globalBias)
        {
            if (lodCount <= 1) return 0;
            float biasScale = std::pow(2.0f, globalBias);
            auto t = [&](int i, bool up) {
                float h = up ? (1.0f + th.hysteresisUp) : (1.0f - th.hysteresisDown);
                return th.Tpx[i] * biasScale * (1.0f - 0.1f * i) * h;
                };
            bool goingUp = (prevLod > 0 && s > th.Tpx[prevLod - 1]);
            if (s > t(0, goingUp)) return 0;
            if (lodCount == 2)     return 1;
            if (s > t(1, goingUp)) return 1;
            if (lodCount == 3)     return 2;
            if (s > t(2, goingUp)) return 2;
            return (std::min)(lodCount - 1, 3);
        }

        // Make occluder thresholds stricter & widen hysteresis to stabilize.
        template<class TTh>
        TTh MakeOccluderThresholds(const TTh& visTh, float scale = 1.25f,
            float up = 0.25f, float down = 0.03f)
        {
            TTh th = visTh;
            for (size_t i = 0; i < th.Tpx.size(); ++i) th.Tpx[i] *= scale;
            th.hysteresisUp = (std::max)(th.hysteresisUp, up);
            th.hysteresisDown = (std::max)(th.hysteresisDown, down);
            return th;
        }

        // Bias occluder decision by the object's render LOD (coarser render LOD -> stricter occluder).
        float OccluderBiasFromRenderLod(int visLod);

        // Screen coverage helper (0..1) from a screen-space AABB.
        float ScreenCoverageFromRectPx(float minx, float miny, float maxx, float maxy,
            float vpW, float vpH);

        float ComputeNDCAreaFrec(float minx, float miny, float maxx, float maxy);

        // Decide occluder LOD using thresholds (recommended).
        template<class TTh>
        OccluderLOD DecideOccluderLOD_FromThresholds(float s_occ,
            const TTh& visTh,
            int prevOccLod /*0..2*/,
            int renderLod /*0..*/,
            float extraGlobalBias = 0.0f)
        {
            TTh occTh = MakeOccluderThresholds(visTh);
            const int occLodCount = 3;
            float globalBias = OccluderBiasFromRenderLod(renderLod) + extraGlobalBias;
            int prevIdx = std::clamp(prevOccLod, 0, occLodCount - 1);
            int idx = SelectLodGeneric(s_occ, occTh, occLodCount, prevIdx, globalBias);
            if (idx <= 0) return OccluderLOD::Near;
            if (idx == 1) return OccluderLOD::Mid;
            return OccluderLOD::Far;
        }

        // Decide occluder LOD by pixel area only (quick heuristic).
        OccluderLOD DecideOccluderLOD_FromArea(float areaPx2);

        struct SoAPosRad {
            const float* px;  // 32B整列が理想
            const float* py;
            const float* pz;
            const float* pr;  // 半径（一定なら nullptr にしてスカラを使う）
            uint32_t     count;
        };

        struct ViewProjParams {
            // Viewの第3行 (row-major, 右掛け想定: v = M[2][0..3])
            float v30, v31, v32, v33;
            // Projectionのdiagonal（対称FOVを仮定しない）
            float P00, P11;
            float zNear, zFar;
            float epsNdc; // これ未満は小さすぎて無視
        };

        inline void CoarseSphereVisible_AVX2(
            const SoAPosRad& s,
            const ViewProjParams& vp,
            std::vector<uint32_t>& outIndices);

    }
} // namespace SectorFW::Graphics
