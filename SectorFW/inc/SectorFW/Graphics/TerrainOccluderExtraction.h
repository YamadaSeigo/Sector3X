#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <array>
#include <cmath>
#include <limits>

// Depends on your math types used in TerrainClustered
// - Math::Vec3f (x,y,z)
// - Math::AABB3f { lb, ub }
// - TerrainClustered / ClusterRange from Graphics/TerrainClustered.h
// This header provides CPU-side utilities to:
//  1) Rank terrain clusters by *screen-space coverage* using AABB + ViewProj + viewport size
//  2) Select LOD per-cluster (distance- and/or coverage-based)
//  3) Expand the chosen LOD's index range into triangles for a CPU soft rasterizer (Masked Occlusion Culling, etc.)
//
// NOTE: Row-major 4x4 matrix is assumed for transforms supplied to this API.

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
        //inline bool GetLodRange_TerrainClustered(const void* terrain, uint32_t clusterId, uint32_t lod, IndexRange& out)
        //{
        //    const auto* T = reinterpret_cast<const TerrainClustered*>(terrain);
        //    if (!T) return false;
        //    if (clusterId >= T->clusters.size()) return false;
        //    const auto& cr = T->clusters[clusterId];
        //    if (lod == 0) {
        //        out.offset = cr.indexOffset;
        //        out.count = cr.indexCount;
        //        return true; // LOD0 は厳密に使用
        //    }
        //    // LOD1 以降は “専用範囲なし” として false → 抽出側が間引き適用
        //    return false;
        //}

        // ------------------------------------------------------------
        // MOC(RenderTriangles) 向けアダプタ
        // ------------------------------------------------------------
        // クリップ空間三角形（x,y,z,w 3頂点）列を、MOC に渡しやすい連続配列へパックします。
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

        inline void MakeSequentialTriangleIndices(uint32_t triCount, std::vector<uint32_t>& outIndices)
        {
            outIndices.resize(triCount * 3);
            for (uint32_t i = 0; i < triCount * 3; ++i) outIndices[i] = i;
        }

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

        // ============================================================
        // AABB Faces の重なり/過剰描画を抑える “貢献度ベース削減”
        // ============================================================
        struct FaceQuad {
            // 4頂点: world, clip, screen
            Math::Vec3f world[4] = {};
            float clip[4][4] = {};
            float sx[4] = {}, sy[4] = {};
            float minDepthNdc = 1.0f; // 近いほど小さい（NDC z）
            float areaPx = 0.f;       // 投影面積（ピクセル）
            int   faceIndex = -1;     // 0..5 (AABBの各面)
            bool  valid = false;
        };

        struct CoverageMask2D {
            uint32_t tilesX = 0, tilesY = 0, tileW = 0, tileH = 0;
            uint32_t screenW = 0, screenH = 0;
            // タイル単位の占有ビット（0/1）と、任意で深度近似（最小NDC z）
            std::vector<uint8_t> occ;    // 占有
            std::vector<float>   minZ;   // そのタイルで既にカバーされた最小NDC z

            void init(uint32_t screenW_, uint32_t screenH_, uint32_t tileW_ = 32, uint32_t tileH_ = 32) {
                screenW = screenW_; screenH = screenH_; tileW = tileW_; tileH = tileH_;
                tilesX = (screenW + tileW - 1) / tileW;
                tilesY = (screenH + tileH - 1) / tileH;
                occ.assign(tilesX * tilesY, 0u);
                minZ.assign(tilesX * tilesY, 1.0f);
            }

            inline uint32_t idx(uint32_t x, uint32_t y) const { return y * tilesX + x; }

            // クアッドのスクリーンAABBを求めて交差タイルを列挙
            uint32_t countUncoveredTilesAndUpdate(const FaceQuad& q, float depthBias = 0.0f,
                float* outAddedRatio = nullptr,
                bool commit = true)
            {
                float minx = +std::numeric_limits<float>::infinity();
                float miny = +std::numeric_limits<float>::infinity();
                float maxx = -std::numeric_limits<float>::infinity();
                float maxy = -std::numeric_limits<float>::infinity();
                for (int i = 0; i < 4; ++i) {
                    minx = (std::min)(minx, q.sx[i]); maxx = (std::max)(maxx, q.sx[i]);
                    miny = (std::min)(miny, q.sy[i]); maxy = (std::max)(maxy, q.sy[i]);
                }
                if (maxx <= 0 || maxy <= 0 || minx >= (float)screenW || miny >= (float)screenH) return 0;
                const int x0 = (std::max)(0, (int)std::floor(minx / (float)tileW));
                const int y0 = (std::max)(0, (int)std::floor(miny / (float)tileH));
                const int x1 = std::min<int>(tilesX - 1, (int)std::floor(maxx / (float)tileW));
                const int y1 = std::min<int>(tilesY - 1, (int)std::floor(maxy / (float)tileH));

                uint32_t added = 0, total = 0;
                for (int ty = y0; ty <= y1; ++ty) for (int tx = x0; tx <= x1; ++tx) {
                    ++total; const uint32_t i = idx(tx, ty);
                    // 深度チェック: そのタイルに既により手前の面があるなら“実質的寄与”が小さい
                    const float curMin = minZ[i];
                    if (q.minDepthNdc - depthBias <= curMin) {
                        if (!occ[i]) { if (commit) occ[i] = 1; ++added; }
                        if (commit) minZ[i] = (std::min)(minZ[i], q.minDepthNdc - depthBias);
                    }
                }
                if (outAddedRatio) *outAddedRatio = (total ? float(added) / float(total) : 0.f);
                return added;
            }
        };

        // AABB6面のうち "見える" 面のみを作成し、タイル被覆の寄与が小さい面を捨てる
        struct AabbFacesReduceOptions {
            // 可視判定: 面法線nと視線方向v（面中心→カメラ）で dot(n,v) > visCos だけ採用
            float visCos = 0.0f;           // 0で“ほぼ正対のみ”。-0.2などで少し緩め可能
            // 追加寄与がこれ未満ならスキップ
            float minAddedTileRatio = 0.15f; // 15%未満の寄与は捨てる
            // タイル解像度
            uint32_t tileW = 32, tileH = 32;
            // 深度バイアス（負値でより厳しく、正でゆるく）
            float depthBias = 0.0f;
            // 最大採用面数/クアッド数（前・側・上のように2〜3面まで等）
            uint32_t maxQuadsPerCluster = 3;
            // スクリーン辺に1ピクセル膨張（保守的）したいとき
            bool dilate1px = true;
        };

        inline void BuildAabbWorldCorners(const Math::AABB3f& b, Math::Vec3f out[8]) {
            out[0] = { b.lb.x, b.lb.y, b.lb.z }; out[1] = { b.ub.x, b.lb.y, b.lb.z };
            out[2] = { b.lb.x, b.ub.y, b.lb.z }; out[3] = { b.ub.x, b.ub.y, b.lb.z };
            out[4] = { b.lb.x, b.lb.y, b.ub.z }; out[5] = { b.ub.x, b.lb.y, b.ub.z };
            out[6] = { b.lb.x, b.ub.y, b.ub.z }; out[7] = { b.ub.x, b.ub.y, b.ub.z };
        }

        // ------------------------------------------------------------
        // クラスター幾何に“内接”する保守的AABB（外側には出さない）
        // 方針: 各軸±方向の法線を持つ頂点群/三角形群から支持点（support）を取り、
        //  元AABBを内側へシュリンク。法線が無い場合はパーセンタイルでの内縮小を使う。
        // ------------------------------------------------------------
        struct ConservativeAabbOpts {
            float normalDotThresh = 0.35f; // 面の向きの判定に使う (cos角度)。0.35≈~69°以内
            float percentile = 0.1f;  // 法線が無い/弱い場合、min/max を内側へ 10% 寄せる
            float maxShrinkFrac = 0.5f;  // 万一の過剰縮小を抑制（元サイズの50%まで）
        };

        inline Math::AABB3f BuildConservativeInnerAabbForCluster(const TerrainClustered& t,
            uint32_t clusterId,
            const ConservativeAabbOpts& co)
        {
            const auto& cr = t.clusters[clusterId];
            const auto* idx = t.indexPool.data() + cr.indexOffset;
            const auto* vtx = t.vertices.data();
            const uint32_t triCount = cr.indexCount / 3;

            // それぞれの軸±向きに対して、内側に置ける“支持点”を集める
            std::vector<float> xsPos, xsNeg, ysPos, ysNeg, zsPos, zsNeg;
            xsPos.reserve(triCount); xsNeg.reserve(triCount);
            ysPos.reserve(triCount); ysNeg.reserve(triCount);
            zsPos.reserve(triCount); zsNeg.reserve(triCount);

            const auto pushIf = [&](const Math::Vec3f& p, const Math::Vec3f& n) {
                // +X / -X
                if (n.x >= co.normalDotThresh) xsPos.push_back(p.x);
                if (-n.x >= co.normalDotThresh) xsNeg.push_back(p.x);
                // +Y / -Y
                if (n.y >= co.normalDotThresh) ysPos.push_back(p.y);
                if (-n.y >= co.normalDotThresh) ysNeg.push_back(p.y);
                // +Z / -Z
                if (n.z >= co.normalDotThresh) zsPos.push_back(p.z);
                if (-n.z >= co.normalDotThresh) zsNeg.push_back(p.z);
                };

            for (uint32_t k = 0; k < cr.indexCount; k += 3) {
                const auto& a = vtx[idx[k + 0]]; const auto& b = vtx[idx[k + 1]]; const auto& c = vtx[idx[k + 2]];
                // 三角形法線（幾何）
                Math::Vec3f ab{ b.pos.x - a.pos.x, b.pos.y - a.pos.y, b.pos.z - a.pos.z };
                Math::Vec3f ac{ c.pos.x - a.pos.x, c.pos.y - a.pos.y, c.pos.z - a.pos.z };
                Math::Vec3f n{ ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
                const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (nl < 1e-18f) continue; n.x /= nl; n.y /= nl; n.z /= nl;
                // 頂点も登録（厳密過ぎないが“外へは出ない”方向の支持点になる）
                pushIf(a.pos, n); pushIf(b.pos, n); pushIf(c.pos, n);
            }

            auto nthOrFallback = [&](std::vector<float>& arr, float defv, bool isMax) {
                if (arr.empty()) return defv;
                size_t nth = std::max<size_t>(0, std::min<size_t>(arr.size() - 1, size_t((isMax ? (1.0f - co.percentile) : co.percentile) * arr.size())));
                std::nth_element(arr.begin(), arr.begin() + nth, arr.end());
                return arr[nth];
                };

            Math::AABB3f inner = t.clusters[clusterId].bounds; // ベースは元AABB
            const auto orig = inner;

            // x-: lb.x を上げ、x+: ub.x を下げる（内側へ）
            const float xn = nthOrFallback(xsNeg, inner.lb.x, false);
            const float xp = nthOrFallback(xsPos, inner.ub.x, true);
            const float yn = nthOrFallback(ysNeg, inner.lb.y, false);
            const float yp = nthOrFallback(ysPos, inner.ub.y, true);
            const float zn = nthOrFallback(zsNeg, inner.lb.z, false);
            const float zp = nthOrFallback(zsPos, inner.ub.z, true);
            inner.lb.x = (std::max)(inner.lb.x, xn);
            inner.ub.x = (std::min)(inner.ub.x, xp);
            inner.lb.y = (std::max)(inner.lb.y, yn);
            inner.ub.y = (std::min)(inner.ub.y, yp);
            inner.lb.z = (std::max)(inner.lb.z, zn);
            inner.ub.z = (std::min)(inner.ub.z, zp);

            // 過剰縮小の安全弁
            const float maxShrinkX = (orig.ub.x - orig.lb.x) * co.maxShrinkFrac;
            const float maxShrinkY = (orig.ub.y - orig.lb.y) * co.maxShrinkFrac;
            const float maxShrinkZ = (orig.ub.z - orig.lb.z) * co.maxShrinkFrac;
            inner.lb.x = (std::min)(inner.lb.x, orig.lb.x + maxShrinkX);
            inner.ub.x = (std::max)(inner.ub.x, orig.ub.x - maxShrinkX);
            inner.lb.y = (std::min)(inner.lb.y, orig.lb.y + maxShrinkY);
            inner.ub.y = (std::max)(inner.ub.y, orig.ub.y - maxShrinkY);
            inner.lb.z = (std::min)(inner.lb.z, orig.lb.z + maxShrinkZ);
            inner.ub.z = (std::max)(inner.ub.z, orig.ub.z - maxShrinkZ);
            // AABBが反転しないように
            inner.lb.x = (std::min)(inner.lb.x, inner.ub.x - 1e-5f);
            inner.lb.y = (std::min)(inner.lb.y, inner.ub.y - 1e-5f);
            inner.lb.z = (std::min)(inner.lb.z, inner.ub.z - 1e-5f);
            return inner;
        };

        // 面ごとの4頂点インデックス
        static constexpr int kFaceCorner[6][4] = {
            {0,1,3,2}, // -Z
            {4,5,7,6}, // +Z
            {0,2,6,4}, // -X
            {1,3,7,5}, // +X
            {0,1,5,4}, // -Y
            {2,3,7,6}  // +Y
        };

        inline Math::Vec3f FaceNormal(int f) {
            switch (f) {
            case 0: return { 0, 0,-1 }; case 1: return { 0, 0, 1 };
            case 2: return { -1, 0, 0 }; case 3: return { 1, 0, 0 };
            case 4: return { 0,-1, 0 }; case 5: return { 0, 1, 0 };
            } return { 0,0,1 };
        }

        inline void ProjectFaceQuad(const Math::Vec3f world[4], const float VP[16], uint32_t vpW, uint32_t vpH,
            FaceQuad& out, bool dilate1px)
        {
            out.valid = true;
            float minx = +1e9, miny = +1e9, maxx = -1e9, maxy = -1e9;
            float minNdcZ = +1e9;
            for (int i = 0; i < 4; ++i) {
                MulRowMajor4x4_Pos(VP, world[i], out.clip[i]);
                const float w = (out.clip[i][3] == 0.f ? 1e-6f : out.clip[i][3]);
                const float nx = std::fmax(-2.f, std::fmin(2.f, out.clip[i][0] / w));
                const float ny = std::fmax(-2.f, std::fmin(2.f, out.clip[i][1] / w));
                const float nz = out.clip[i][2] / w; // NDC z
                out.sx[i] = (nx * 0.5f + 0.5f) * vpW;
                out.sy[i] = (1.f - (ny * 0.5f + 0.5f)) * vpH;
                minx = (std::min)(minx, out.sx[i]); maxx = (std::max)(maxx, out.sx[i]);
                miny = (std::min)(miny, out.sy[i]); maxy = (std::max)(maxy, out.sy[i]);
                minNdcZ = (std::min)(minNdcZ, nz);
            }
            if (dilate1px) { minx -= 1; miny -= 1; maxx += 1; maxy += 1; }
            const float wpx = std::fmax(0.f, maxx - minx);
            const float hpx = std::fmax(0.f, maxy - miny);
            out.areaPx = wpx * hpx;
            out.minDepthNdc = minNdcZ;
            if (out.areaPx <= 0.f) out.valid = false;
        }

        // クラスターのAABBから、可視な面のみを貢献度ベースで厳選し、最大N面だけ採用
        inline void ReduceAabbFacesForCluster(const Math::AABB3f& bounds,
            const Math::Vec3f& camPos,
            const float VP[16], uint32_t vpW, uint32_t vpH,
            const AabbFacesReduceOptions& ropt,
            CoverageMask2D& mask,
            std::vector<FaceQuad>& outQuads)
        {
            outQuads.clear();
            Math::Vec3f c[8]; BuildAabbWorldCorners(bounds, c);

            struct Cand { FaceQuad q; float score = 0.0f; };
            std::array<Cand, 6> cand; uint32_t candN = 0;
            const Math::Vec3f center{ (bounds.lb.x + bounds.ub.x) * 0.5f, (bounds.lb.y + bounds.ub.y) * 0.5f, (bounds.lb.z + bounds.ub.z) * 0.5f };
            Math::Vec3f viewDir{ camPos.x - center.x, camPos.y - center.y, camPos.z - center.z };
            const float vlen = std::sqrt(viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z);
            if (vlen > 1e-9f) { viewDir.x /= vlen; viewDir.y /= vlen; viewDir.z /= vlen; }

            for (int f = 0; f < 6; ++f) {
                const Math::Vec3f n = FaceNormal(f);
                const float cosv = (n.x * viewDir.x + n.y * viewDir.y + n.z * viewDir.z);
                if (cosv <= ropt.visCos) continue; // 不可視（ほぼ背面）は除外

                FaceQuad fq; Math::Vec3f w[4];
                for (int i = 0; i < 4; ++i) w[i] = c[kFaceCorner[f][i]];
                ProjectFaceQuad(w, VP, vpW, vpH, fq, ropt.dilate1px);
                if (!fq.valid) continue;
                fq.faceIndex = f;
                const float score = (std::max)(0.f, cosv) * fq.areaPx;
                cand[candN++] = { fq, score };
            }
            if (!candN) return;

            std::sort(cand.begin(), cand.begin() + candN, [](const Cand& a, const Cand& b) { return a.score > b.score; });

            for (uint32_t i = 0; i < candN && outQuads.size() < ropt.maxQuadsPerCluster; ++i) {
                float addedRatio = 0.f;
                const uint32_t added = mask.countUncoveredTilesAndUpdate(cand[i].q, ropt.depthBias, &addedRatio, /*commit=*/false);
                if (added == 0 || addedRatio < ropt.minAddedTileRatio) {
                    continue;
                }
                (void)mask.countUncoveredTilesAndUpdate(cand[i].q, ropt.depthBias, nullptr, /*commit=*/true);
                outQuads.push_back(cand[i].q);
            }
        }

        // ---- AabbFaces モードの強化抽出（貢献度ベース） ----
        void ExtractOccluderTriangles_AabbFacesReduced(
            const TerrainClustered& t,
            const OccluderExtractOptions& opt,
            const AabbFacesReduceOptions& ropt,
            std::vector<uint32_t>& outClusterIds,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip)
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

            CoverageMask2D mask; mask.init(opt.viewportW, opt.viewportH, ropt.tileW, ropt.tileH);
            outClusterIds.reserve(keep);

            auto emitQuad = [&](const FaceQuad& q) {
                const int order[6] = { 0,1,2, 2,1,3 };
                for (int tix = 0; tix < 6; tix += 3) {
					if (tix > 4) break;
                    const int i0 = order[tix + 0];
                    const int i1 = order[tix + 1];
                    const int i2 = order[tix + 2];
                    if (outTrisClip) {
                        SoftTriClip tc{};
                        std::memcpy(tc.v0, q.clip[i0], sizeof(float) * 4);
                        std::memcpy(tc.v1, q.clip[i1], sizeof(float) * 4);
                        std::memcpy(tc.v2, q.clip[i2], sizeof(float) * 4);
                        outTrisClip->push_back(tc);
                    }
                    outTrisWorld.push_back({ q.world[i0], q.world[i1], q.world[i2] });
                }
                };

            // ここで “内接AABB” を使って AabbFaces を作ることで、
            // クラスタ外へはみ出す（=過剰遮蔽になる）問題を抑制する。
            ConservativeAabbOpts co{}; // デフォルト閾値

            for (uint32_t i = 0; i < keep; ++i) {
                const uint32_t cid = sc[i].id; outClusterIds.push_back(cid);
                const auto inner = BuildConservativeInnerAabbForCluster(t, cid, co);

                // 内接AABBから可視面を選別＆貢献度判定
                std::vector<FaceQuad> quads;
                ReduceAabbFacesForCluster(inner, opt.cameraPos, opt.viewProj, opt.viewportW, opt.viewportH, ropt, mask, quads);
                for (const auto& q : quads) emitQuad(q);
            }
        }

        // ============================================================
        // 高さマップ地形用：粗グリッド実サーフェスから直接オクルーダー生成
        // ============================================================
        // クラスター領域のXZを粗グリッド (gx x gz) に分割し、各格子点の高さを HeightSampler から取得。
        // 上向きスロープ（法線のY成分がしきい値以上）だけをクアッド=2三角で出力します。

        using HeightSampler = float(*)(float x, float z, void* user); // world XZ -> world Y

        struct HeightCoarseOptions {
            uint32_t gridX = 4;           // X方向の分割数（クアッド数）。頂点は gridX+1
            uint32_t gridZ = 4;           // Z方向の分割数
            float    upDotMin = 0.65f;    // 上向き判定のしきい値（n·(0,1,0) >= upDotMin）
            float    maxSlopeTan = 10.0f; // 異常スロープ除外（高さ差/水平長の上限）。0で無効
            float    heightClampMin = -std::numeric_limits<float>::infinity();
            float    heightClampMax = +std::numeric_limits<float>::infinity();
            bool     makeClipSpace = true; // クリップ空間三角も同時に生成
        };

        // 法線計算（中心差分）
        inline Math::Vec3f CalcGridNormal(float hl, float hr, float hd, float hu, float dx, float dz)
        {
            // dH/dx ~ (hr - hl)/(2*dx), dH/dz ~ (hu - hd)/(2*dz)
            const float ddx = (hr - hl) / (2.0f * dx);
            const float ddz = (hu - hd) / (2.0f * dz);
            // 位置関数は y = H(x,z) → 勾配ベクトル = (ddx, 1, ddz) に対応する面法線は (-ddx, 1, -ddz) を正規化
            Math::Vec3f n{ -ddx, 1.0f, -ddz };
            const float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (L > 0.0f) { n.x /= L; n.y /= L; n.z /= L; }
            return n;
        }

        // --- 高さテクスチャの座標系（ワールド→テクスチャUV）を保持する記述子 ---
        struct HeightTexMapping {
            // height テクスチャ（row-major: v行×u列）。16bit/8bit等の場合は外で正規化して float 配列に。
            const float* tex = nullptr;
            int texW = 0, texH = 0;     // テクスチャサイズ

            // ワールド→UV 変換:  u = (x - originX) * worldToTexU + uOffset
            //                     v = (z - originZ) * worldToTexV + vOffset
            float originX = 0.f, originZ = 0.f; // ワールド原点（地形タイルの左下など）
            float worldToTexU = 1.f, worldToTexV = 1.f; // 1/worldUnitsPerTexel
            float uOffset = 0.f, vOffset = 0.f;         // タイルオフセット（タイルインデックス由来）

            // 高さスケール・オフセット（テクスチャ→ワールドY）
            float heightScale = 1.f; // 例: テクスチャが[0,1]ならワールドメートルへのスケール
            float heightOffset = 0.f;

            // サンプリングモード
            bool clampUV = true; // true: クランプ, false: リピート
        };

        // 生成済み H01（0..1 高さ配列）を使って HeightTexMapping を組み立てるユーティリティ
        inline HeightTexMapping MakeHeightTexMappingFromTerrainParams(
            const TerrainBuildParams& p,
            const std::vector<float>& H01  // TerrainClustered::Build(..., &H01) で受け取った配列
        ) {
            using namespace SFW::Graphics;
            HeightTexMapping m{};

            // 高さ“テクスチャ”の実体は H01（row-major: y*W + x）
            m.tex = H01.data();
            m.texW = static_cast<int>(p.cellsX + 1); // 頂点数 = セル数+1
            m.texH = static_cast<int>(p.cellsZ + 1);

            // ワールド→テクセル変換： 1texel = cellSize [m]
            m.originX = 0.0f;  // TerrainClustered は (x*cellSize, z*cellSize) で配置
            m.originZ = 0.0f;  // → 原点は (0,0) でよい
            m.worldToTexU = 1.0f / p.cellSize; // u = (x - originX) * (1/cellSize)
            m.worldToTexV = 1.0f / p.cellSize; // v = (z - originZ) * (1/cellSize)

            // 単一大域テクスチャなのでタイルオフセットは 0（タイル運用ならここに加算）
            m.uOffset = 0.0f;
            m.vOffset = 0.0f;

            // 高さ 0..1 → ワールドY： y = h * heightScale + heightOffset
            m.heightScale = p.heightScale;
            m.heightOffset = 0.0f;

            // タイル境界での外挿は避けたいので基本はクランプ
            m.clampUV = true;

            return m;
        }

        inline int clampi(int x, int lo, int hi) { return (x < lo ? lo : (x > hi ? hi : x)); }
        inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

        // バイリニアサンプル（HeightTexMapping→float 高さ）
        inline float SampleHeight_Bilinear(const HeightTexMapping& m, float x, float z)
        {
            float u = (x - m.originX) * m.worldToTexU + m.uOffset;
            float v = (z - m.originZ) * m.worldToTexV + m.vOffset;
            if (m.clampUV) {
                u = std::fmax(0.f, std::fmin(u, float(m.texW - 1)));
                v = std::fmax(0.f, std::fmin(v, float(m.texH - 1)));
            }
            else {
                // リピート
                auto wrap = [](float t, float n) { t = std::fmod(t, n); if (t < 0) t += n; return t; };
                u = wrap(u, float(m.texW - 1));
                v = wrap(v, float(m.texH - 1));
            }
            const int x0 = (int)std::floor(u); const int x1 = clampi(x0 + 1, 0, m.texW - 1);
            const int y0 = (int)std::floor(v); const int y1 = clampi(y0 + 1, 0, m.texH - 1);
            const float tu = u - float(x0);
            const float tv = v - float(y0);
            const float h00 = m.tex[y0 * m.texW + x0];
            const float h10 = m.tex[y0 * m.texW + x1];
            const float h01 = m.tex[y1 * m.texW + x0];
            const float h11 = m.tex[y1 * m.texW + x1];
            const float h0 = lerp(h00, h10, tu);
            const float h1 = lerp(h01, h11, tu);
            const float h = lerp(h0, h1, tv);
            return h * m.heightScale + m.heightOffset;
        }

        // HeightSampler 互換のブリッジ
        inline float HeightSampler_FromMapping(float x, float z, void* user)
        {
            const HeightTexMapping& map = *reinterpret_cast<const HeightTexMapping*>(user);
            return SampleHeight_Bilinear(map, x, z);
        }

        // --- 自動グリッドLOD: 画面占有面積からセル数を決める ---
        struct AutoGridLodOpts {
            uint32_t minCells = 2;  // 軸方向の最少セル数
            uint32_t maxCells = 8;  // 軸方向の最大セル数
            float targetCellPx = 24.f; // セル1辺の目標ピクセル
        };

        inline void ChooseGridForCluster(const Math::AABB3f& bounds,
            const OccluderExtractOptions& opt,
            const AutoGridLodOpts& aopt,
            uint32_t& outGX, uint32_t& outGZ)
        {
            const float area = AABBScreenAreaPx(bounds, opt.viewProj, opt.viewportW, opt.viewportH);
            const float sidePx = std::sqrt((std::max)(1.f, area));
            const float cellsF = sidePx / (std::max)(1.f, aopt.targetCellPx);
            const uint32_t cells = (uint32_t)std::round(std::fmax((float)aopt.minCells, std::fmin((float)aopt.maxCells, cellsF)));
            outGX = outGZ = std::max<uint32_t>(1, cells);
        }

        // --- 高さバイアス（過剰遮蔽対策） ---
        struct HeightBiasOpts {
            float baseDown = 0.0f;   // 常に下げる量（m）
            float slopeK = 0.0f;   // 斜面が急なほど下げる係数（cellDiag * tanSlope * slopeK）
        };

        // グリッドサーフェス構築（座標系マッピング＋自動LOD＋バイアス対応版）
        struct HeightCoarseOptions2 : public HeightCoarseOptions {
            AutoGridLodOpts gridLod;
            HeightBiasOpts  bias;
        };

        inline Math::Vec3f CalcGridNormalFromSamples(float hl, float hr, float hd, float hu, float dx, float dz)
        {
            return CalcGridNormal(hl, hr, hd, hu, dx, dz);
        }

        inline void BuildHeightCoarseSurfaceForCluster_Mapped(
            const Math::AABB3f& bounds,
            const HeightTexMapping& map,
            HeightCoarseOptions2 hopt,
            const OccluderExtractOptions& opt,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip)
        {
            // 自動LOD
            ChooseGridForCluster(bounds, opt, hopt.gridLod, hopt.gridX, hopt.gridZ);

            const uint32_t gx = std::max<uint32_t>(1, hopt.gridX);
            const uint32_t gz = std::max<uint32_t>(1, hopt.gridZ);
            const float x0 = bounds.lb.x, x1 = bounds.ub.x;
            const float z0 = bounds.lb.z, z1 = bounds.ub.z;
            const float dx = (x1 - x0) / float(gx);
            const float dz = (z1 - z0) / float(gz);

            // グリッド頂点の高さをサンプル
            std::vector<float> H((gx + 1) * (gz + 1));
            for (uint32_t iz = 0; iz <= gz; ++iz) {
                const float z = z0 + dz * float(iz);
                for (uint32_t ix = 0; ix <= gx; ++ix) {
                    const float x = x0 + dx * float(ix);
                    float h = SampleHeight_Bilinear(map, x, z);
                    h = std::fmax(hopt.heightClampMin, std::fmin(h, hopt.heightClampMax));
                    H[iz * (gx + 1) + ix] = h;
                }
            }

            auto P = [&](uint32_t ix, uint32_t iz) {
                const float x = x0 + dx * float(ix);
                const float z = z0 + dz * float(iz);
                const float y = H[iz * (gx + 1) + ix];
                return Math::Vec3f{ x,y,z };
                };

            const float cellDiag = std::sqrt(dx * dx + dz * dz);

            auto emitTri = [&](const Math::Vec3f& a, const Math::Vec3f& c, const Math::Vec3f& b) {
                if (opt.backfaceCull && !IsFrontFacing(a, b, c, opt.cameraPos, opt.faceCosThreshold)) return;
                outTrisWorld.push_back({ a,b,c });
                if (outTrisClip && opt.makeClipSpace && opt.viewProj) {
                    SoftTriClip tc{}; MulRowMajor4x4_Pos(opt.viewProj, a, tc.v0); MulRowMajor4x4_Pos(opt.viewProj, b, tc.v1); MulRowMajor4x4_Pos(opt.viewProj, c, tc.v2); outTrisClip->push_back(tc);
                }
                };

            for (uint32_t iz = 0; iz < gz; ++iz) {
                for (uint32_t ix = 0; ix < gx; ++ix) {
                    // 4隅の高さ
                    const float h00 = H[(iz + 0) * (gx + 1) + (ix + 0)];
                    const float h10 = H[(iz + 0) * (gx + 1) + (ix + 1)];
                    const float h01 = H[(iz + 1) * (gx + 1) + (ix + 0)];
                    const float h11 = H[(iz + 1) * (gx + 1) + (ix + 1)];

                    // 中心差分で法線
                    const float hl = h00, hr = h10, hd = h00, hu = h01; // 近傍代理（厳密でなくてOK）
                    Math::Vec3f n = CalcGridNormalFromSamples(hl, hr, hd, hu, dx, dz);
                    if (n.y < hopt.upDotMin) continue; // 上向きでなければスキップ

                    // スロープタンジェント（近似）
                    const float tanSlope = std::sqrt((std::max)(0.f, (1.f - n.y * n.y))) / (std::max)(1e-6f, n.y);
                    if (hopt.maxSlopeTan > 0.f && tanSlope > hopt.maxSlopeTan) continue;

                    // 高さバイアス（下方向へ）
                    auto bias = [&](float y) { return y - (hopt.bias.baseDown + hopt.bias.slopeK * tanSlope * cellDiag); };

                    Math::Vec3f p00 = P(ix + 0, iz + 0); p00.y = bias(p00.y);
                    Math::Vec3f p10 = P(ix + 1, iz + 0); p10.y = bias(p10.y);
                    Math::Vec3f p01 = P(ix + 0, iz + 1); p01.y = bias(p01.y);
                    Math::Vec3f p11 = P(ix + 1, iz + 1); p11.y = bias(p11.y);

                    // 2三角（対角は適宜選択）。ここでは (00,10,01) と (01,10,11)
                    emitTri(p00, p10, p01);
                    emitTri(p01, p10, p11);
                }
            }
        }

        // --- ハイブリッド: 高さ（粗サーフェス） ---
        inline void ExtractOccluderTriangles_HeightmapCoarse_Hybrid(
            const TerrainClustered& t,
            const HeightTexMapping& map,
            HeightCoarseOptions2 hopt,
            const OccluderExtractOptions& opt,
            std::vector<uint32_t>& outClusterIds,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip)
        {
            outClusterIds.clear(); outTrisWorld.clear(); if (outTrisClip) outTrisClip->clear();
            if (!opt.viewProj) return;

            // 画面占有率でクラスタ選別
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

            // 前提: sc[] は {id, area, d2} が詰まっている
            constexpr struct CenterBiasOptions {
                bool  enable = true;
                float sigmaPx = 320.0f;   // 中心からの距離に対するガウス幅（px）
                float gain = 1.0f;     // バイアス強度（0で無効）
                uint32_t reserveCenterN = 8;   // 中心窓から必ず確保する数（0で無効）
                float   centerWindowFrac = 2.0f; // 画面中央の長方形（幅/高さの比）
            } copt; // 必要に応じて外から渡してもOK

            auto ProjectPointToScreen = [&](const Math::Vec3f& p, float& sx, float& sy)->bool {
                float h[4];
                MulRowMajor4x4_Pos(opt.viewProj, p, h);
                if (h[3] <= 0.0f) return false;
                const float nx = std::fmax(-2.f, std::fmin(2.f, h[0] / h[3]));
                const float ny = std::fmax(-2.f, std::fmin(2.f, h[1] / h[3]));
                sx = (nx * 0.5f + 0.5f) * float(opt.viewportW);
                sy = (1.f - (ny * 0.5f + 0.5f)) * float(opt.viewportH);
                return true;
                };

            struct Scored2 {
                uint32_t id; float area; float score; bool inCenter;
            };
            std::vector<Scored2> ranked; ranked.reserve(sc.size());

            const float cx = 0.5f * float(opt.viewportW);
            const float cy = 0.5f * float(opt.viewportH);
            const float winW = float(opt.viewportW) * copt.centerWindowFrac;
            const float winH = float(opt.viewportH) * copt.centerWindowFrac;
            const float left = cx - 0.5f * winW, right = cx + 0.5f * winW;
            const float top = cy - 0.5f * winH, bottom = cy + 0.5f * winH;

            for (const auto& s : sc) {
                const auto& cr = t.clusters[s.id];
                // AABB中心を画面座標へ（近似で十分）
                const Math::Vec3f ctr{ (cr.bounds.lb.x + cr.bounds.ub.x) * 0.5f,
                                       (cr.bounds.lb.y + cr.bounds.ub.y) * 0.5f,
                                       (cr.bounds.lb.z + cr.bounds.ub.z) * 0.5f };
                float sx = 0, sy = 0; bool vis = ProjectPointToScreen(ctr, sx, sy);

                float score = s.area;
                bool inside = false;
                if (vis && copt.enable && copt.gain > 0.f && copt.sigmaPx > 0.f) {
                    const float dx = sx - cx, dy = sy - cy;
                    const float r2 = dx * dx + dy * dy;
                    const float sig2 = copt.sigmaPx * copt.sigmaPx;
                    const float boost = copt.gain * std::exp(-r2 / (2.0f * sig2));
                    score = s.area * (1.0f + boost); // 中心に近いほどスコア加点
                    inside = (sx >= left && sx <= right && sy >= top && sy <= bottom);
                }
                ranked.push_back({ s.id, s.area, score, inside });
            }

            // スコア降順で並べる
            std::sort(ranked.begin(), ranked.end(),
                [](const Scored2& a, const Scored2& b) { return a.score > b.score; });

            // 収容上限
            const uint32_t cap = (opt.maxClusters > 0)
                ? std::min<uint32_t>((uint32_t)ranked.size(), opt.maxClusters)
                : (uint32_t)ranked.size();

            outClusterIds.clear();
            outClusterIds.reserve(cap);

            // 中心窓から N 個“先取り”
            if (copt.enable && copt.reserveCenterN > 0) {
                uint32_t taken = 0;
                for (const auto& r : ranked) {
                    if (!r.inCenter) continue;
                    outClusterIds.push_back(r.id);
                    if (++taken >= copt.reserveCenterN || outClusterIds.size() >= cap) break;
                }
            }

            // 残りはスコア順で埋める（重複回避）
            auto already = [&](uint32_t id) {
                return std::find(outClusterIds.begin(), outClusterIds.end(), id) != outClusterIds.end();
                };
            for (const auto& r : ranked) {
                if (outClusterIds.size() >= cap) break;
                if (already(r.id)) continue;
                outClusterIds.push_back(r.id);
            }

            // “keep で ranked を再プッシュ”は不要！
            // ここからは outClusterIds を使って三角形を生成するだけ。
            for (uint32_t i = 0; i < outClusterIds.size(); ++i) {
                const uint32_t cid = outClusterIds[i];
                const auto& cr = t.clusters[cid];

                // 高さの粗サーフェスのみを出力（必要ならここに側面1枚の処理を足す）
                BuildHeightCoarseSurfaceForCluster_Mapped(cr.bounds, map, hopt, opt, outTrisWorld, outTrisClip);
            }
        }

        // --- ハイブリッド: 高さ（粗サーフェス）＋ AABB側面を1面だけ補助 ---
        inline void ExtractOccluderTriangles_HeightmapCoarse_Hybrid(
            const TerrainClustered& t,
            const HeightTexMapping& map,
            HeightCoarseOptions2 hopt,
            const OccluderExtractOptions& opt,
            const AabbFacesReduceOptions& sideOpt, // maxQuadsPerCluster=1 を推奨
            std::vector<uint32_t>& outClusterIds,
            std::vector<SoftTriWorld>& outTrisWorld,
            std::vector<SoftTriClip>* outTrisClip)
        {
            outClusterIds.clear(); outTrisWorld.clear(); if (outTrisClip) outTrisClip->clear();
            if (!opt.viewProj) return;

            // 画面占有率でクラスタ選別
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

            // タイルカバレッジ用（側面1枚の寄与評価）
            CoverageMask2D mask; mask.init(opt.viewportW, opt.viewportH, sideOpt.tileW, sideOpt.tileH);

            outClusterIds.reserve(keep);

            auto emitQuad = [&](const FaceQuad& q) {
                constexpr int order[6] = { 0,3,1, 2,1,3 };
                for (int tix = 0; tix < 6; tix += 3) {
					if (tix > 4) break;
                    const int i0 = order[tix + 0], i1 = order[tix + 1], i2 = order[tix + 2];
                    if (outTrisClip) {
                        SoftTriClip tc{};
                        std::memcpy(tc.v0, q.clip[i0], sizeof(float) * 4);
                        std::memcpy(tc.v1, q.clip[i1], sizeof(float) * 4);
                        std::memcpy(tc.v2, q.clip[i2], sizeof(float) * 4);
                        outTrisClip->push_back(tc);
                    }
                    outTrisWorld.push_back({ q.world[i0], q.world[i1], q.world[i2] });
                }
                };

            for (uint32_t i = 0; i < keep; ++i) {
                const uint32_t cid = sc[i].id; outClusterIds.push_back(cid);
                const auto& cr = t.clusters[cid];

                // 1) 高さの粗サーフェス
                BuildHeightCoarseSurfaceForCluster_Mapped(cr.bounds, map, hopt, opt, outTrisWorld, outTrisClip);

                // 2) AABB側面 1枚だけ（寄与が大きいなら）
                AabbFacesReduceOptions one = sideOpt; one.maxQuadsPerCluster = 1; // 念のため
                std::vector<FaceQuad> quads;
                ReduceAabbFacesForCluster(cr.bounds, opt.cameraPos, opt.viewProj, opt.viewportW, opt.viewportH, one, mask, quads);
                for (const auto& q : quads) emitQuad(q);
            }
        }
    }
} // namespace SFW::Graphics
