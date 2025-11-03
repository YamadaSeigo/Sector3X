#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <array>
#include <cmath>
#include <limits>

#include "TerrainClustered.h"
#include "../Math/AABB.hpp"

namespace SFW {
    namespace Graphics {

        struct SoftTriWorld {
            Math::Vec3f v0, v1, v2; // world-space positions
        };

        struct SoftTriClip {
            // Homogeneous clip-space vertices (x,y,z,w) = ViewProj * (world,1)
            float v0[4];
            float v1[4];
            float v2[4];
        };

        // Utility: distance^2 from point to AABB
        inline float Dist2PointAABB(const Math::Vec3f& p, const Math::AABB3f& b)
        {
            float dx = 0.f; if (p.x < b.lb.x) dx = b.lb.x - p.x; else if (p.x > b.ub.x) dx = p.x - b.ub.x;
            float dy = 0.f; if (p.y < b.lb.y) dy = b.lb.y - p.y; else if (p.y > b.ub.y) dy = p.y - b.ub.y;
            float dz = 0.f; if (p.z < b.lb.z) dz = b.lb.z - p.z; else if (p.z > b.ub.z) dz = p.z - b.ub.z;
            return dx * dx + dy * dy + dz * dz;
        }

        inline void MulRowMajor4x4_Pos(const float m[16], const Math::Vec3f& p, float out[4])
        {
            out[0] = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
            out[1] = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
            out[2] = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
            out[3] = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
        }

        // Project 8 corners of an AABB to NDC and then to screen, return covered pixel area (conservative rectangle)
        // Returns 0 if completely behind the near plane (w <= 0 for all corners) or if the rectangle is degenerate.
        inline float AABBScreenAreaPx(const Math::AABB3f& b, const float viewProj[16], uint32_t vpW, uint32_t vpH)
        {
            // 8 corners
            Math::Vec3f c[8] = {
                {b.lb.x, b.lb.y, b.lb.z}, {b.ub.x, b.lb.y, b.lb.z}, {b.lb.x, b.ub.y, b.lb.z}, {b.ub.x, b.ub.y, b.lb.z},
                {b.lb.x, b.lb.y, b.ub.z}, {b.ub.x, b.lb.y, b.ub.z}, {b.lb.x, b.ub.y, b.ub.z}, {b.ub.x, b.ub.y, b.ub.z}
            };
            float minx = +std::numeric_limits<float>::infinity();
            float miny = +std::numeric_limits<float>::infinity();
            float maxx = -std::numeric_limits<float>::infinity();
            float maxy = -std::numeric_limits<float>::infinity();
            bool anyInFront = false;
            for (int i = 0; i < 8; ++i) {
                float h[4];
                MulRowMajor4x4_Pos(viewProj, c[i], h);
                if (h[3] > 0.0f) anyInFront = true; // at least one corner in front of near plane
                // If h.w <= 0, push it slightly in front (very conservative clamp) to keep bounds usable
                const float w = (h[3] == 0.0f ? 1e-6f : h[3]);
                const float ndcX = h[0] / w;
                const float ndcY = h[1] / w;
                // clamp ndc to [-2,2] to avoid numeric explosion when far behind; still conservative
                const float nx = std::fmax(-2.f, std::fmin(2.f, ndcX));
                const float ny = std::fmax(-2.f, std::fmin(2.f, ndcY));
                const float sx = (nx * 0.5f + 0.5f) * float(vpW);
                const float sy = (1.0f - (ny * 0.5f + 0.5f)) * float(vpH); // y-down screen
                minx = std::fmin(minx, sx); maxx = std::fmax(maxx, sx);
                miny = std::fmin(miny, sy); maxy = std::fmax(maxy, sy);
            }
            if (!anyInFront) return 0.0f;
            const float wpx = std::fmax(0.0f, maxx - minx);
            const float hpx = std::fmax(0.0f, maxy - miny);
            const float area = wpx * hpx;
            return (area > 1.0f ? area : 0.0f);
        }

        // LOD selection policy (customizable)
        struct ILodSelector {
            virtual ~ILodSelector() = default;
            virtual uint32_t selectLOD(uint32_t clusterId, float distance, float screenAreaPx) const = 0;
        };

        // Simple default policy:
        //  - Prefer coverage-based thresholds; fallback to distance if coverage is small.
        struct DefaultLodSelector final : ILodSelector {
            // coverage thresholds in pixels (descending importance)
            float covToLOD1 = 30'000.f; // if area < covToLOD1 => LOD1 or higher (simpler)
            float covToLOD2 = 8'000.f;  // if area < covToLOD2 => LOD2
            // distance thresholds (meters)
            float distToLOD1 = 120.f;
            float distToLOD2 = 220.f;
            uint32_t selectLOD(uint32_t /*clusterId*/, float distance, float area) const override {
                if (area < covToLOD2 || distance > distToLOD2) return 2;
                if (area < covToLOD1 || distance > distToLOD1) return 1;
                return 0; // highest detail
            }
        };

        // Optional: tell the extractor how to obtain index range for a given cluster + LOD
        struct IndexRange { uint32_t offset = 0, count = 0; };
        using LodRangeGetter = bool(*)(const void* terrain /*TerrainClustered*/, uint32_t clusterId, uint32_t lod, IndexRange& out);

        // Options for extraction
        struct OccluderExtractOptions {
            // --- ranking / filtering ---
            uint32_t viewportW = 1920;
            uint32_t viewportH = 1080;
            const float* viewProj = nullptr;  // row-major 4x4 (16 floats)
            float minAreaPx = 64.0f;          // clusters whose AABB projects to less than this area are skipped
            uint32_t maxClusters = 256;       // after sorting by area, keep at most this many clusters

            // --- distance (for LOD decision) ---
            Math::Vec3f cameraPos{ 0,0,0 };
            float maxDistance = 0.0f; // 0 => unlimited; used only as a hard cull before ranking

            // --- triangle filtering ---
            bool backfaceCull = true;       // discard triangles whose geometric normal faces away from camera
            float faceCosThreshold = 0.0f;  // if backfaceCull: keep triangles with dot(n, viewDir) >= threshold (0 keeps front-facing)

            // --- outputs ---
            bool makeClipSpace = true;      // also produce homogeneous clip-space tris

            // --- LOD hookup ---
            const ILodSelector* lodSelector = nullptr; // nullptr => use DefaultLodSelector()
            LodRangeGetter getLodRange = nullptr;      // if nullptr, use cluster.indexOffset/indexCount regardless of LOD
            const void* terrainForGetter = nullptr;    // pass &terrain if using getLodRange

            // --- crude fallback decimation when getLodRange == nullptr ---
            // If lod=1 or 2 is chosen but no per-LOD index ranges are available, we can decimate by triangle stride.
            // e.g., lodDecimate[1] = 2 => take every 2nd triangle; lodDecimate[2] = 4 => every 4th triangle.
            uint32_t lodDecimate[3] = { 1, 2, 4 };

            // --- extreme reduction knobs ---
            enum class OccluderMode : uint8_t {
                Full = 0,         // use full mesh (respecting LOD ranges if available)
                Decimate = 1,     // stride-based decimation (plus target triangle budgets)
                AabbFaces = 2,    // use 12-triangle AABB proxy (6 quads split to 2 tris each)
                AabbFrontQuad = 3 // use 2-triangle front-most quad of the AABB only
            };
            OccluderMode mode = OccluderMode::Decimate;

            // If mode==Decimate, try to limit triangles per cluster aggressively
            // (applied after LOD / range resolution). 0 means no per-cluster target.
            uint32_t targetTrianglesPerCluster = 128; // very small default

            // Global hard budget across all clusters; 0 means unlimited. Counts emitted world-space triangles
            // (clip-spaceも同数で増えます)。
            uint32_t maxTrianglesTotal = 5000;
        };

        // Internal: backface check
        inline bool IsFrontFacing(const Math::Vec3f& a, const Math::Vec3f& b, const Math::Vec3f& c,
            const Math::Vec3f& cameraPos, float cosThresh)
        {
            Math::Vec3f ab{ b.x - a.x, b.y - a.y, b.z - a.z };
            Math::Vec3f ac{ c.x - a.x, c.y - a.y, c.z - a.z };
            Math::Vec3f n{ ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
            Math::Vec3f v{ cameraPos.x - a.x, cameraPos.y - a.y, cameraPos.z - a.z };
            const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            const float vl = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (nl <= 1e-18f || vl <= 1e-18f) return false;
            const float dot = (n.x * v.x + n.y * v.y + n.z * v.z) / (nl * vl);
            return (dot >= cosThresh);
        }

        // Core API: rank clusters by *screen coverage*, LOD-select, then expand triangles.
        // outClusterIds: kept clusters in descending priority (coverage)
        void ExtractOccluderTriangles_ScreenCoverageLOD(
            const TerrainClustered& t,
            const OccluderExtractOptions& opt,
            std::vector<uint32_t>& outClusterIds,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip // optional; may be nullptr
        )
        {
            outClusterIds.clear();
            outTrisWorld.clear();
            if (outTrisClip) outTrisClip->clear();

            // Preconditions
            if (!opt.viewProj) return; // need VP for coverage ranking

            // 1) Score clusters by screen-space AABB area
            struct Scored { uint32_t id; float area; float d2; };
            std::vector<Scored> sc; sc.reserve(t.clusters.size());

            const float maxD2 = (opt.maxDistance > 0.0f) ? (opt.maxDistance * opt.maxDistance) : std::numeric_limits<float>::infinity();

            for (uint32_t id = 0; id < (uint32_t)t.clusters.size(); ++id) {
                const auto& cr = t.clusters[id];
                const float d2 = Dist2PointAABB(opt.cameraPos, cr.bounds);
                if (d2 > maxD2) continue; // hard-cut by distance
                const float areaPx = AABBScreenAreaPx(cr.bounds, opt.viewProj, opt.viewportW, opt.viewportH);
                if (areaPx < opt.minAreaPx) continue; // too tiny
                sc.push_back({ id, areaPx, d2 });
            }
            if (sc.empty()) return; // nothing significant

            // Highest coverage first
            const uint32_t keep = std::min<uint32_t>((uint32_t)sc.size(), opt.maxClusters);
            std::nth_element(sc.begin(), sc.begin() + keep - 1, sc.end(), [](const Scored& a, const Scored& b) { return a.area > b.area; });
            std::sort(sc.begin(), sc.begin() + keep, [](const Scored& a, const Scored& b) { return a.area > b.area; });

            outClusterIds.reserve(keep);

            // 2) LOD selector
            DefaultLodSelector defaultSel;
            const ILodSelector& selector = opt.lodSelector ? *opt.lodSelector : (const ILodSelector&)defaultSel;

            // 3) Expand triangles from the chosen LOD
            auto addTri = [&](const Math::Vec3f& a, const Math::Vec3f& b, const Math::Vec3f& c) {
                if (opt.backfaceCull && !IsFrontFacing(a, b, c, opt.cameraPos, opt.faceCosThreshold)) return;
                outTrisWorld.push_back({ a,b,c });
                if (outTrisClip && opt.makeClipSpace && opt.viewProj) {
                    SoftTriClip tc{};
                    MulRowMajor4x4_Pos(opt.viewProj, a, tc.v0);
                    MulRowMajor4x4_Pos(opt.viewProj, b, tc.v1);
                    MulRowMajor4x4_Pos(opt.viewProj, c, tc.v2);
                    outTrisClip->push_back(tc);
                }
                };

            const auto* verts = t.vertices.data();
            const auto* idx = t.indexPool.data();

            for (uint32_t i = 0; i < keep; ++i) {
                const uint32_t cid = sc[i].id; outClusterIds.push_back(cid);
                const auto& cr = t.clusters[cid];

                // distance for LOD
                const float dist = std::sqrt(sc[i].d2);
                const uint32_t lod = selector.selectLOD(cid, dist, sc[i].area);

                // Index range for chosen LOD
                IndexRange range;
                bool haveRange = false;
                if (opt.getLodRange) {
                    haveRange = opt.getLodRange(opt.terrainForGetter ? opt.terrainForGetter : (const void*)&t, cid, lod, range);
                }
                if (!haveRange) {
                    // fallback to cluster's base range and optionally decimate
                    range.offset = cr.indexOffset;
                    range.count = cr.indexCount;

                    const uint32_t stride = (lod < 3 ? std::max<uint32_t>(1, opt.lodDecimate[lod]) : opt.lodDecimate[2]);
                    if (stride > 1) {
                        // Emit every 'stride' triangle
                        for (uint32_t k = 0, tri = 0; k + 2 < range.count; k += 3, ++tri) {
                            if ((tri % stride) != 0) continue;
                            const auto& a = verts[idx[range.offset + k + 0]].pos;
                            const auto& b = verts[idx[range.offset + k + 1]].pos;
                            const auto& c = verts[idx[range.offset + k + 2]].pos;
                            addTri(a, b, c);
                        }
                        continue;
                    }
                }

                // Emit full triangles from the chosen LOD range
                for (uint32_t k = 0; k + 2 < range.count; k += 3) {
                    const auto& a = verts[idx[range.offset + k + 0]].pos;
                    const auto& b = verts[idx[range.offset + k + 1]].pos;
                    const auto& c = verts[idx[range.offset + k + 2]].pos;
                    addTri(a, b, c);
                }
            }
        }

        // ------------------------------------------------------------
        // getLodRange 実装（TerrainClustered 向け）
        // ------------------------------------------------------------
        // 現状の TerrainClustered はクラスター毎に 1つのインデックス範囲のみを持つため、
        // LOD0 のときだけ厳密な範囲を返し、LOD1/2 などは false を返して呼び出し側の
        // フォールバック間引き（lodDecimate）に委ねます。
        inline bool GetLodRange_TerrainClustered(const void* terrain, uint32_t clusterId, uint32_t lod, IndexRange& out)
        {
            const auto* T = reinterpret_cast<const TerrainClustered*>(terrain);
            if (!T) return false;
            if (clusterId >= T->clusters.size()) return false;
            const auto& cr = T->clusters[clusterId];
            if (lod == 0) {
                out.offset = cr.indexOffset;
                out.count = cr.indexCount;
                return true; // LOD0 は厳密に使用
            }
            // LOD1 以降は “専用範囲なし” として false → 抽出側が間引き適用
            return false;
        }

        // ------------------------------------------------------------
        // MOC(RenderTriangles) 向けアダプタ
        // ------------------------------------------------------------
        // クリップ空間三角形（x,y,z,w 3頂点）列を、MOC に渡しやすい連続配列へパックします。
        // 具体的な MOC の関数シグネチャにはいくつかバリエーションがあるため、ここでは
        // 「3頂点×4要素の連続配列（float）」に変換し、必要ならインデックスも用意します。
        // 呼び出し側で持っている MOC の RenderTriangles の形に合わせてご利用ください。

        // outXYZW: [v0.x v0.y v0.z v0.w, v1.x ... v1.w, v2.x ... v2.w,  v0.x ...]（三角形ごと連結）
        inline void PackClipTrianglesForMOC(const std::vector<SoftTriClip>& trisClip,
            std::vector<float>& outXYZW)
        {
            outXYZW.clear();
            outXYZW.reserve(trisClip.size() * 12);
            for (const auto& t : trisClip) {
                outXYZW.insert(outXYZW.end(), t.v0, t.v0 + 4);
                outXYZW.insert(outXYZW.end(), t.v1, t.v1 + 4);
                outXYZW.insert(outXYZW.end(), t.v2, t.v2 + 4);
            }
        }

        // MOC のバリアントによっては頂点配列＋インデックス配列で受けるものもあるため、
        // 単純な 3頂点ポリゴン列を [0..N*3-1] のインデックスで構築します。
        inline void MakeSequentialTriangleIndices(uint32_t triCount, std::vector<uint32_t>& outIndices)
        {
            outIndices.resize(triCount * 3);
            for (uint32_t i = 0; i < triCount * 3; ++i) outIndices[i] = i;
        }

        // 例: 任意の RenderTriangles 互換コールバックに渡すアダプタ
        // RenderTrianglesFn の想定は、(clip-vertices packed, vertexCount, optional index/data)のような形。
        // 実際の MOC API に合わせてラムダを用意し、このアダプタを呼び出してください。
        using RenderTrianglesFn = void(*)(const float* packedXYZW, uint32_t vertexCount,
            const uint32_t* indices, uint32_t indexCount,
            uint32_t viewportW, uint32_t viewportH);

        inline void DispatchToMOC(auto fn,
            const std::vector<SoftTriClip>& trisClip,
            uint32_t viewportW, uint32_t viewportH)
        {
            if (trisClip.empty()) return;
            std::vector<float> packed;
            PackClipTrianglesForMOC(trisClip, packed);
            std::vector<uint32_t> indices;
            MakeSequentialTriangleIndices((uint32_t)trisClip.size(), indices);
            fn(packed.data(), (uint32_t)packed.size() / 4, indices.data(), (uint32_t)indices.size(), viewportW, viewportH);
        }

        // ------------------------------------------------------------
        // Aggressive reduction variant: supports triangle budgets and AABB proxies
        // ------------------------------------------------------------
        inline void ExtractOccluderTriangles_ScreenCoverageLOD_Budgeted(
            const TerrainClustered& t,
            const OccluderExtractOptions& opt,
            std::vector<uint32_t>& outClusterIds,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip // optional
        )
        {
            outClusterIds.clear();
            outTrisWorld.clear();
            if (outTrisClip) outTrisClip->clear();
            if (!opt.viewProj) return;

            struct Scored { uint32_t id; float area; float d2; };
            std::vector<Scored> sc; sc.reserve(t.clusters.size());
            const float maxD2 = (opt.maxDistance > 0.0f) ? (opt.maxDistance * opt.maxDistance) : std::numeric_limits<float>::infinity();
            for (uint32_t id = 0; id < (uint32_t)t.clusters.size(); ++id) {
                const auto& cr = t.clusters[id];
                const float d2 = Dist2PointAABB(opt.cameraPos, cr.bounds);
                if (d2 > maxD2) continue;
                const float areaPx = AABBScreenAreaPx(cr.bounds, opt.viewProj, opt.viewportW, opt.viewportH);
                if (areaPx < opt.minAreaPx) continue;
                sc.push_back({ id, areaPx, d2 });
            }
            if (sc.empty()) return;
            const uint32_t keep = std::min<uint32_t>((uint32_t)sc.size(), opt.maxClusters);
            std::nth_element(sc.begin(), sc.begin() + keep - 1, sc.end(), [](const Scored& a, const Scored& b) { return a.area > b.area; });
            std::sort(sc.begin(), sc.begin() + keep, [](const Scored& a, const Scored& b) { return a.area > b.area; });
            outClusterIds.reserve(keep);

            DefaultLodSelector defaultSel;
            const ILodSelector& selector = opt.lodSelector ? *opt.lodSelector : (const ILodSelector&)defaultSel;
            const auto* verts = t.vertices.data();
            const auto* idx = t.indexPool.data();

            //CCW
            auto addTri = [&](const Math::Vec3f& a, const Math::Vec3f& c, const Math::Vec3f& b) {
                if (opt.backfaceCull && !IsFrontFacing(a, b, c, opt.cameraPos, opt.faceCosThreshold)) return;
                outTrisWorld.push_back({ a,b,c });
                if (outTrisClip && opt.makeClipSpace && opt.viewProj) {
                    SoftTriClip tc{};
                    MulRowMajor4x4_Pos(opt.viewProj, a, tc.v0);
                    MulRowMajor4x4_Pos(opt.viewProj, b, tc.v1);
                    MulRowMajor4x4_Pos(opt.viewProj, c, tc.v2);
                    outTrisClip->push_back(tc);
                }
                };

            auto emit_aabb_faces = [&](const Math::AABB3f& B) {
                const Math::Vec3f p000{ B.lb.x,B.lb.y,B.lb.z };
                const Math::Vec3f p100{ B.ub.x,B.lb.y,B.lb.z };
                const Math::Vec3f p010{ B.lb.x,B.ub.y,B.lb.z };
                const Math::Vec3f p110{ B.ub.x,B.ub.y,B.lb.z };
                const Math::Vec3f p001{ B.lb.x,B.lb.y,B.ub.z };
                const Math::Vec3f p101{ B.ub.x,B.lb.y,B.ub.z };
                const Math::Vec3f p011{ B.lb.x,B.ub.y,B.ub.z };
                const Math::Vec3f p111{ B.ub.x,B.ub.y,B.ub.z };
                auto quad = [&](const Math::Vec3f& a, const Math::Vec3f& b, const Math::Vec3f& c, const Math::Vec3f& d) { addTri(a, b, c); addTri(a, c, d); };
                quad(p000, p100, p110, p010); // -Z
                quad(p001, p011, p111, p101); // +Z
                quad(p000, p010, p011, p001); // -X
                quad(p100, p101, p111, p110); // +X
                quad(p000, p001, p101, p100); // -Y
                quad(p010, p110, p111, p011); // +Y
                };
            auto emit_aabb_front_quad = [&](const Math::AABB3f& B) {
                Math::Vec3f center{ (B.lb.x + B.ub.x) * 0.5f, (B.lb.y + B.ub.y) * 0.5f, (B.lb.z + B.ub.z) * 0.5f };
                Math::Vec3f d{ center.x - opt.cameraPos.x, center.y - opt.cameraPos.y, center.z - opt.cameraPos.z };
                Math::Vec3f n[6] = { {0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0} };
                float best = -1e9f; int face = 0;
                auto dot3 = [&](const Math::Vec3f& a, const Math::Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
                for (int f = 0; f < 6; ++f) { float s = -dot3(n[f], d); if (s > best) { best = s; face = f; } }
                const Math::Vec3f p000{ B.lb.x,B.lb.y,B.lb.z };
                const Math::Vec3f p100{ B.ub.x,B.lb.y,B.lb.z };
                const Math::Vec3f p010{ B.lb.x,B.ub.y,B.lb.z };
                const Math::Vec3f p110{ B.ub.x,B.ub.y,B.lb.z };
                const Math::Vec3f p001{ B.lb.x,B.lb.y,B.ub.z };
                const Math::Vec3f p101{ B.ub.x,B.lb.y,B.ub.z };
                const Math::Vec3f p011{ B.lb.x,B.ub.y,B.ub.z };
                const Math::Vec3f p111{ B.ub.x,B.ub.y,B.ub.z };
                auto quad = [&](const Math::Vec3f& a, const Math::Vec3f& b, const Math::Vec3f& c, const Math::Vec3f& d) { addTri(a, b, c); addTri(a, c, d); };
                switch (face) {
                case 0: quad(p000, p100, p110, p010); break; // -Z
                case 1: quad(p001, p011, p111, p101); break; // +Z
                case 2: quad(p000, p010, p011, p001); break; // -X
                case 3: quad(p100, p101, p111, p110); break; // +X
                case 4: quad(p000, p001, p101, p100); break; // -Y
                default:quad(p010, p110, p111, p011); break; // +Y
                }
                };

            uint32_t triBudget = opt.maxTrianglesTotal;
            for (uint32_t i = 0; i < keep; ++i) {
                const uint32_t cid = sc[i].id; outClusterIds.push_back(cid);
                const auto& cr = t.clusters[cid];

                // Proxy modes
                if (opt.mode == OccluderExtractOptions::OccluderMode::AabbFaces) {
                    emit_aabb_faces(cr.bounds);
                    if (triBudget && outTrisWorld.size() >= triBudget) break;
                    continue;
                }
                if (opt.mode == OccluderExtractOptions::OccluderMode::AabbFrontQuad) {
                    emit_aabb_front_quad(cr.bounds);
                    if (triBudget && outTrisWorld.size() >= triBudget) break;
                    continue;
                }

                // Mesh-backed modes
                const float dist = std::sqrt(sc[i].d2);
                const uint32_t lod = selector.selectLOD(cid, dist, sc[i].area);

                IndexRange range; bool haveRange = false;
                if (opt.getLodRange) haveRange = opt.getLodRange(opt.terrainForGetter ? opt.terrainForGetter : (const void*)&t, cid, lod, range);
                uint32_t stride = 1;
                if (!haveRange) {
                    range.offset = cr.indexOffset;
                    range.count = cr.indexCount;
                    stride = (lod < 3 ? std::max<uint32_t>(1, opt.lodDecimate[lod]) : opt.lodDecimate[2]);
                }
                // Decimate mode may further increase stride to satisfy targetTrianglesPerCluster
                if (opt.mode == OccluderExtractOptions::OccluderMode::Decimate && opt.targetTrianglesPerCluster > 0) {
                    if (range.count / 3 > opt.targetTrianglesPerCluster) {
                        const uint32_t desired = opt.targetTrianglesPerCluster;
                        stride = std::max<uint32_t>(stride, std::max<uint32_t>(1, (range.count / 3) / desired));
                    }
                }

                uint32_t tri = 0; uint32_t emitted = 0;
                for (uint32_t k = 0; k + 2 < range.count; k += 3, ++tri) {
                    if (opt.mode != OccluderExtractOptions::OccluderMode::Full) {
                        if ((tri % stride) != 0) continue;
                    }
                    const auto& a = verts[idx[range.offset + k + 0]].pos;
                    const auto& b = verts[idx[range.offset + k + 1]].pos;
                    const auto& c = verts[idx[range.offset + k + 2]].pos;
                    addTri(a, b, c); ++emitted;
                    if (triBudget && outTrisWorld.size() >= triBudget) break;
                }
                if (triBudget && outTrisWorld.size() >= triBudget) break;
            }
        }
    }
} // namespace SFW::Graphics
