#include "Graphics/TerrainClustered.h"

namespace SFW {

    namespace Graphics
    {
        namespace
        {
            using float3 = Math::Vec3f;
			using float2 = Math::Vec2f;

            static float3 make3(float x, float y, float z) { return { x,y,z }; }
            static float2 make2(float x, float y) { return { x,y }; }

            static float3 cross(const float3& a, const float3& b) {
                return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
            }
            static void normalize(float3& v) {
                float s = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (s > 1e-20f) { v.x /= s; v.y /= s; v.z /= s; }
            }

            static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
            static inline float dot3(const SFW::Math::Vec3f& a, const SFW::Math::Vec3f& b) {
                return a.x * b.x + a.y * b.y + a.z * b.z;
            }
            static inline SFW::Math::Vec3f add3(const SFW::Math::Vec3f& a, const SFW::Math::Vec3f& b) {
                return { a.x + b.x, a.y + b.y, a.z + b.z };
            }
            static inline SFW::Math::Vec3f sub3(const SFW::Math::Vec3f& a, const SFW::Math::Vec3f& b) {
                return { a.x - b.x, a.y - b.y, a.z - b.z };
            }
            static inline SFW::Math::Vec3f mul3(const SFW::Math::Vec3f& a, float s) {
                return { a.x * s, a.y * s, a.z * s };
            }
            static inline SFW::Math::Vec3f lerp3(const SFW::Math::Vec3f& a, const SFW::Math::Vec3f& b, float t) {
                return add3(a, mul3(sub3(b, a), t));
            }
            static inline float radians(float deg) { return deg * 3.1415926535f / 180.f; }

            // 姿勢基底を“回転制限 + 上向きバイアス”で作るユーティリティ
            static void build_basis_with_up_bias(
                const SFW::Math::Vec3f& upDesired,  // 地形から得た上方向（=平面法線など）
                float maxTiltDeg,
                float upBias,                       // 0..1 （1に近いほど上向きに）
                SFW::Math::Vec3f& outUp             // 正規化済みUpを返す
            ) {
                using V3 = SFW::Math::Vec3f;
                V3 upWorld = { 0,1,0 };

                // upDesired を maxTilt でクランプ
                float c = clamp01(dot3(upDesired, upWorld));
                float angle = std::acos(c);
                float aClamped = std::min(angle, radians(maxTiltDeg));
                // 目標Up（法線側）を、クランプ角で補間
                V3 axis = cross(upWorld, upDesired);
                float axLen = std::sqrt(dot3(axis, axis));
                V3 upClamped = upWorld;
                if (axLen > 1e-6f) {
                    axis = mul3(axis, 1.0f / axLen);
                    // Rodrigues
                    float ca = std::cos(aClamped), sa = std::sin(aClamped);
                    V3 k = axis;
                    V3 term1 = mul3(upWorld, ca);
                    V3 term2 = mul3(cross(k, upWorld), sa);
                    V3 term3 = mul3(k, dot3(k, upWorld) * (1 - ca));
                    upClamped = add3(add3(term1, term2), term3);
                    float s = std::sqrt(dot3(upClamped, upClamped));
                    if (s > 1e-12f) upClamped = mul3(upClamped, 1.0f / s);
                }

                // 上向きバイアスでさらに upWorld へ寄せる
                outUp = lerp3(upClamped, upWorld, clamp01(upBias));
                float s = std::sqrt(dot3(outUp, outUp));
                if (s > 1e-12f) outUp = mul3(outUp, 1.0f / s);
                else          outUp = upWorld;
            }

            // --- 最小二乗で h(x,z)=a x + b z + c を解くユーティリティ ---
            struct PlaneLsq { float a, b, c; }; // h = a x + b z + c
            static bool FitPlaneLsq(const std::vector<SFW::Math::Vec3f>& pts, PlaneLsq& out)
            {
                using V3 = SFW::Math::Vec3f;
                const size_t N = pts.size();
                if (N < 3) return false;

                double Sx = 0, Sz = 0, Sxx = 0, Szz = 0, Sxz = 0, Sy = 0, Sxy = 0, Szy = 0;
                for (auto& p : pts) {
                    double x = p.x, z = p.z, y = p.y;
                    Sx += x;  Sz += z;  Sy += y;
                    Sxx += x * x; Szz += z * z; Sxz += x * z;
                    Sxy += x * y; Szy += z * y;
                }
                double n = double(N);

                // 正規方程式（2x2 + 右辺）を解く
                // [Sxx Sxz][a] = [Sxy - c*Sx]
                // [Sxz Szz][b]   [Szy - c*Sz]
                // ただし c は  y平均 = a*x平均 + b*z平均 + c から
                // => c = (Sy/n) - a*(Sx/n) - b*(Sz/n)
                // まず a,b を c 消去の形で直接解く（平均中心化して解くと安定）
                double mx = Sx / n, mz = Sz / n, my = Sy / n;

                // 中心化量
                double Cxx = 0, Cxz = 0, Czz = 0, Cxy = 0, Czy = 0;
                for (auto& p : pts) {
                    double x = p.x - mx, z = p.z - mz, y = p.y - my;
                    Cxx += x * x; Czz += z * z; Cxz += x * z;
                    Cxy += x * y; Czy += z * y;
                }
                double det = Cxx * Czz - Cxz * Cxz;
                if (std::abs(det) < 1e-12) return false; // 退化

                double inv00 = Czz / det, inv01 = -Cxz / det;
                double inv10 = -Cxz / det, inv11 = Cxx / det;

                double a = inv00 * Cxy + inv01 * Czy;
                double b = inv10 * Cxy + inv11 * Czy;
                double c = my - a * mx - b * mz;

                out = { float(a), float(b), float(c) };
                return true;
            }
        }

        TerrainClustered TerrainClustered::Build(const TerrainBuildParams& p, std::vector<float>* outMap)
        {
            TerrainClustered t{};
            t.vertsX = p.cellsX + 1;
            t.vertsZ = p.cellsZ + 1;

            // 1) 高さ場
            std::vector<float> H;

            GenerateHeights(H, t.vertsX, t.vertsZ, p);

            // 2) 頂点（位置・法線・UV）
            BuildVertices(t.vertices, H, t.vertsX, t.vertsZ, p.cellSize, p.heightScale, p.offset);

            // 3) クラスターを作って IndexPool に連結
            BuildClusters(t.indexPool, t.clusters, t.clustersX, t.clustersZ,
                H, p.cellsX, p.cellsZ, p.clusterCellsX, p.clusterCellsZ, p.cellSize, p.heightScale, p.offset);


            if (outMap != nullptr) *outMap = std::move(H);

            return t;
        }

        TerrainClustered TerrainClustered::BuildFromHeightMap(const HeightField& hf, const TerrainBuildParams& p)
        {
            TerrainClustered t{};
            t.vertsX = hf.vertsX;
            t.vertsZ = hf.vertsZ;

            // 1) 頂点生成（既存関数）
            BuildVertices(t.vertices, hf.H01, t.vertsX, t.vertsZ, p.cellSize, p.heightScale, p.offset);

            // 2) クラスター生成（既存関数）
            const uint32_t cellsX = t.vertsX - 1, cellsZ = t.vertsZ - 1;
            BuildClusters(t.indexPool, t.clusters, t.clustersX, t.clustersZ,
                hf.H01, cellsX, cellsZ, p.clusterCellsX, p.clusterCellsZ,
                p.cellSize, p.heightScale, p.offset);

            return t;
        }

        // 量子化XZで境界だけ溶接（全体でも可）
        // 前提: vertices = TerrainClustered::vertices (pos/nrm/uv)
        //       indexPool = TerrainClustered::indexPool
        struct XZKey { int32_t qx, qz; };
        static inline XZKey makeKey(const SFW::Graphics::TerrainVertex& v, float cellSize) {
            const float q = 1.0f / cellSize; // グリッド量子化
            return { (int32_t)std::lround(v.pos.x * q), (int32_t)std::lround(v.pos.z * q) };
        }

        void TerrainClustered::WeldVerticesAlongBorders(std::vector<SFW::Graphics::TerrainVertex>& vertices, std::vector<uint32_t>& indexPool, float cellSize)
        {
            const size_t vcount = vertices.size();
            std::vector<uint32_t> remap(vcount);
            remap.assign(vcount, UINT32_MAX);

            // 量子化XZ→代表頂点 の辞書
            struct Hasher {
                size_t operator()(const XZKey& k) const noexcept {
                    return (size_t)k.qx * 1469598103934665603ull ^ (size_t)k.qz;
                }
            };
            struct Eq {
                bool operator()(const XZKey& a, const XZKey& b) const noexcept {
                    return a.qx == b.qx && a.qz == b.qz;
                }
            };
            std::unordered_map<XZKey, uint32_t, Hasher, Eq> map;
            map.reserve(vcount);

            // 代表IDを決める（先に出た頂点を代表に）
            for (uint32_t vid = 0; vid < vcount; ++vid) {
                XZKey k = makeKey(vertices[vid], cellSize);
                auto it = map.find(k);
                if (it == map.end()) {
                    map.emplace(k, vid);
                    remap[vid] = vid;
                }
                else {
                    remap[vid] = it->second; // 代表へ寄せる
                }
            }

            // IndexPool を代表IDに差し替え（完全共有化）
            for (uint32_t& idx : indexPool) {
                const uint32_t r = remap[idx];
                if (r != UINT32_MAX) idx = r;
            }

            // 代表IDの y に合わせる
            for (uint32_t vid = 0; vid < vertices.size(); ++vid) {
                uint32_t r = remap[vid];
                if (r != UINT32_MAX && r != vid) {
                    vertices[vid].pos.y = vertices[r].pos.y; // 代表高さへ強制一致
                }
            }
        }

        bool TerrainClustered::CheckClusterBorderEquality(const std::vector<uint32_t>& indexPool, const std::vector<TerrainClustered::ClusterRange>& clusters, uint32_t clustersX, uint32_t clustersZ)
        {
            auto edgeSet = [&](const TerrainClustered::ClusterRange& r, int side) {
                // side: 0=left,1=right,2=bottom,3=top
                std::vector<std::pair<uint32_t, uint32_t>> E; E.reserve(r.indexCount / 3);
                auto add = [&](uint32_t a, uint32_t b) { if (a > b) std::swap(a, b); E.emplace_back(a, b); };
                const uint32_t* idx = indexPool.data() + r.indexOffset;
                for (uint32_t k = 0; k + 2 < r.indexCount; k += 3) {
                    uint32_t i0 = idx[k], i1 = idx[k + 1], i2 = idx[k + 2];
                    // 辺が“境界側の頂点だけで構成される”なら登録（実装はあなたの境界判定を使う）
                    // ここでは簡略化: 3頂点のうち境界判定がtrueの頂点どうしのペアを追加
                    // isBorderVertex(i, side) を用意して利用
                }
                std::sort(E.begin(), E.end());
                E.erase(std::unique(E.begin(), E.end()), E.end());
                return E;
                };

            auto idAt = [&](uint32_t x, uint32_t z) { return z * clustersX + x; };
            for (uint32_t z = 0; z < clustersZ; ++z) {
                for (uint32_t x = 0; x < clustersX; ++x) {
                    const auto& me = clusters[idAt(x, z)];
                    if (x + 1 < clustersX) {
                        const auto& nb = clusters[idAt(x + 1, z)];
                        auto L = edgeSet(me, /*right*/1);
                        auto R = edgeSet(nb, /*left*/0);
                        if (L != R) return false; // 一致していない → 共有崩れ
                    }
                    if (z + 1 < clustersZ) {
                        const auto& nb = clusters[idAt(x, z + 1)];
                        auto B = edgeSet(me, /*top*/3);
                        auto T = edgeSet(nb, /*bottom*/2);
                        if (B != T) return false;
                    }
                }
            }
            return true;
        }

        void TerrainClustered::AddSkirtsToClusters(SFW::Graphics::TerrainClustered& t, float skirtDepth)
        {
            using namespace SFW::Graphics;
            auto VIdx = TerrainClustered::VIdx;

            if (t.vertices.empty() || t.clusters.empty() || t.vertsX < 2 || t.vertsZ < 2) return;

            // --- cellSizeX/Z を推定（規則グリッド前提）
            const float baseX = t.vertices[0].pos.x;
            float cellSizeX = 0.f, cellSizeZ = 0.f;
            // X 方向
            for (uint32_t x = 1; x < t.vertsX; ++x) {
                float d = t.vertices[VIdx(x, 0, t.vertsX)].pos.x - baseX;
                if (std::abs(d) > 1e-12f) { cellSizeX = d / float(x); break; }
            }
            // Z 方向
            const float baseZ = t.vertices[0].pos.z;
            for (uint32_t z = 1; z < t.vertsZ; ++z) {
                float d = t.vertices[VIdx(0, z, t.vertsX)].pos.z - baseZ;
                if (std::abs(d) > 1e-12f) { cellSizeZ = d / float(z); break; }
            }

            auto toGridX = [&](float x)->int { return int(std::lround(x / cellSizeX)); };
            auto toGridZ = [&](float z)->int { return int(std::lround(z / cellSizeZ)); };

            // 既存頂点 → 追加スカート頂点のIDマップ（重複防止）
            std::vector<uint32_t> skirtBottomOf(t.vertices.size(), UINT32_MAX);

            // 追加頂点を push する関数
            auto ensureBottomVertex = [&](uint32_t topVid)->uint32_t {
                uint32_t& ref = skirtBottomOf[topVid];
                if (ref != UINT32_MAX) return ref;

                TerrainVertex v = t.vertices[topVid];
                v.pos.y -= skirtDepth;           // 下へ押し出す
                // 法線/UVはとりあえずコピー。必要なら法線は再計算や水平化も検討
                ref = (uint32_t)t.vertices.size();
                t.vertices.push_back(v);
                return ref;
                };

            // クラスタごとに 4辺のストリップを張り、indexPool の末尾に追加
            for (auto& cr : t.clusters) {
                const auto& b = cr.bounds;
                const int x0 = toGridX(b.lb.x);
                const int x1 = toGridX(b.ub.x);
                const int z0 = toGridZ(b.lb.z);
                const int z1 = toGridZ(b.ub.z);

                // このクラスタの新規インデックス追加個数のカウンタ（最後に cr.indexCount へ加算する）
                uint32_t added = 0;

                auto addQuad = [&](uint32_t i0, uint32_t i1) {
                    // 上辺の i0-i1 と、下に複製した b0-b1 で二枚三角
                    const uint32_t b0 = ensureBottomVertex(i0);
                    const uint32_t b1 = ensureBottomVertex(i1);
                    // (i0, i1, b1), (i0, b1, b0)
                    t.indexPool.push_back(i0); t.indexPool.push_back(i1); t.indexPool.push_back(b1);
                    t.indexPool.push_back(i0); t.indexPool.push_back(b1); t.indexPool.push_back(b0);
                    added += 6;
                    };

                // 上辺（z=z0, x: x0→x1-1）
                for (int x = x0; x < x1; ++x) {
                    uint32_t i0 = VIdx((uint32_t)x, (uint32_t)z0, t.vertsX);
                    uint32_t i1 = VIdx((uint32_t)(x + 1), (uint32_t)z0, t.vertsX);
                    addQuad(i0, i1);
                }
                // 下辺（z=z1）
                for (int x = x0; x < x1; ++x) {
                    uint32_t i0 = VIdx((uint32_t)x, (uint32_t)z1, t.vertsX);
                    uint32_t i1 = VIdx((uint32_t)(x + 1), (uint32_t)z1, t.vertsX);
                    addQuad(i0, i1);
                }
                // 左辺（x=x0, z: z0→z1-1）
                for (int z = z0; z < z1; ++z) {
                    uint32_t i0 = VIdx((uint32_t)x0, (uint32_t)z, t.vertsX);
                    uint32_t i1 = VIdx((uint32_t)x0, (uint32_t)(z + 1), t.vertsX);
                    addQuad(i0, i1);
                }
                // 右辺（x=x1）
                for (int z = z0; z < z1; ++z) {
                    uint32_t i0 = VIdx((uint32_t)x1, (uint32_t)z, t.vertsX);
                    uint32_t i1 = VIdx((uint32_t)x1, (uint32_t)(z + 1), t.vertsX);
                    addQuad(i0, i1);
                }

                // このクラスタの描画範囲（IndexPool上）を拡張
                cr.indexCount += added;
                // indexOffset は既存のまま（末尾に追加していくため）
            }
        }

        void TerrainClustered::InitSplatDefault(uint32_t commonSplatTexId, const uint32_t materialIds[4], const float tilingUV[4][2], float splatScaleU, float splatScaleV, float splatOffsetU, float splatOffsetV)
        {
            // クラスターが確定済み前提
            splat.resize(clusters.size());

            for (uint32_t cid = 0; cid < (uint32_t)clusters.size(); ++cid) {
                ClusterSplatMeta meta{};
                meta.layerCount = 4;
                for (uint32_t i = 0; i < 4; ++i) {
                    meta.layers[i].materialId = materialIds[i];
                    meta.layers[i].uvTilingU = tilingUV[i][0];
                    meta.layers[i].uvTilingV = tilingUV[i][1];
                }
                meta.splatTextureId = commonSplatTexId; // 全クラスターで同じスプラットでもOK（あとで差し替え可）
                meta.splatUVScaleU = splatScaleU;
                meta.splatUVScaleV = splatScaleV;
                meta.splatUVOffsetU = splatOffsetU;
                meta.splatUVOffsetV = splatOffsetV;
                splat[cid] = meta;
            }
        }

        void TerrainClustered::InitSplatWithGenerator(const SplatGenerator& gen)
        {
            splat.resize(clusters.size());
            for (uint32_t cid = 0; cid < (uint32_t)clusters.size(); ++cid) {
                splat[cid] = gen(cid, clusters[cid]);
            }
        }

        bool TerrainClustered::SampleHeightNormalBilinear(float x, float z, float& outH, Math::Vec3f* outN) const
        {
            if (vertices.empty() || vertsX < 2 || vertsZ < 2) return false;

            // cellSizeX/Z を推定（等間隔グリッド前提）
            const float baseX = vertices[0].pos.x;
            float cellSizeX = 0.f, cellSizeZ = 0.f;
            for (uint32_t ix = 1; ix < vertsX; ++ix) {
                float d = vertices[VIdx(ix, 0, vertsX)].pos.x - baseX;
                if (std::abs(d) > 1e-12f) { cellSizeX = d / float(ix); break; }
            }
            const float baseZ = vertices[0].pos.z;
            for (uint32_t iz = 1; iz < vertsZ; ++iz) {
                float d = vertices[VIdx(0, iz, vertsX)].pos.z - baseZ;
                if (std::abs(d) > 1e-12f) { cellSizeZ = d / float(iz); break; }
            }
            if (cellSizeX <= 0.f || cellSizeZ <= 0.f) return false;

            // ワールド→グリッド座標
            float gx = x / cellSizeX;
            float gz = z / cellSizeZ;

            int ix = (int)std::floor(gx);
            int iz = (int)std::floor(gz);
            float fx = gx - (float)ix;
            float fz = gz - (float)iz;

            if (ix < 0 || iz < 0 || ix + 1 >= (int)vertsX || iz + 1 >= (int)vertsZ) return false;

            auto V = [&](int X, int Z) { return vertices[VIdx((uint32_t)X, (uint32_t)Z, vertsX)]; };

            // 高さ（Y）をバイリニア
            float h00 = V(ix, iz).pos.y;
            float h10 = V(ix + 1, iz).pos.y;
            float h01 = V(ix, iz + 1).pos.y;
            float h11 = V(ix + 1, iz + 1).pos.y;
            float hx0 = h00 * (1.f - fx) + h10 * fx;
            float hx1 = h01 * (1.f - fx) + h11 * fx;
            outH = hx0 * (1.f - fz) + hx1 * fz;

            if (outN) {
                // 法線もバイリニア（頂点法線は BuildVertices で三角加算→正規化済み）
                auto n00 = V(ix, iz).nrm;
                auto n10 = V(ix + 1, iz).nrm;
                auto n01 = V(ix, iz + 1).nrm;
                auto n11 = V(ix + 1, iz + 1).nrm;
                Math::Vec3f nx0 = { n00.x * (1 - fx) + n10.x * fx, n00.y * (1 - fx) + n10.y * fx, n00.z * (1 - fx) + n10.z * fx };
                Math::Vec3f nx1 = { n01.x * (1 - fx) + n11.x * fx, n01.y * (1 - fx) + n11.y * fx, n01.z * (1 - fx) + n11.z * fx };
                Math::Vec3f n = { nx0.x * (1 - fz) + nx1.x * fz, nx0.y * (1 - fz) + nx1.y * fz, nx0.z * (1 - fz) + nx1.z * fz };
                float s = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (s > 1e-12f) { n.x /= s; n.y /= s; n.z /= s; }
                else { n = { 0,1,0 }; }
                *outN = n;
            }
            return true;
        }

        TerrainClustered::RigidPose TerrainClustered::SolvePlacementByAnchors(
            const Math::Vec3f& basePosWS,
            float yawRad,
            float scale,
            const std::vector<Math::Vec2f>& anchorsLocalXZ,
            float maxTiltDeg,
            float upBias,
            float baseBias
        ) const
        {
            using V2 = Math::Vec2f;
            using V3 = Math::Vec3f;

            RigidPose out{};

            // 0) 水平基準軸（yaw）を作成（+Z 前想定）
            const float cy = std::cos(yawRad), sy = std::sin(yawRad);
            const V3 rightH = { cy, 0.0f, -sy }; // yaw右
            const V3 forwardH = { sy, 0.0f,  cy }; // yaw前（+Z前）
            const V3 upW = { 0, 1, 0 };

            // 1) アンカーの世界XZ & 高さサンプル、ついでに (s,t) を計算
            struct Sample { double s, t, y; };
            std::vector<Sample> sam; sam.reserve(anchorsLocalXZ.size());

            auto dotXZ = [](const V3& a, const V3& b) { return double(a.x * b.x + a.z * b.z); };

            for (auto& a : anchorsLocalXZ) {
                // ローカルXZ → yaw/scale → 世界XZ
                V2 al = { a.x * scale, a.y * scale };
                V2 ar = { al.x * cy - al.y * sy,  al.x * sy + al.y * cy }; // yaw回転
                float wx = basePosWS.x + ar.x;
                float wz = basePosWS.z + ar.y;

                // 高さ（バイリニア）
                float h = 0.f; V3 n;
                if (!SampleHeightNormalBilinear(wx, wz, h, &n)) continue;

                // base を原点とした水平座標から (s,t) を算出
                V3 P = { wx, 0.0f, wz };
                V3 P0 = { basePosWS.x, 0.0f, basePosWS.z };
                V3 d = { P.x - P0.x, 0.0f, P.z - P0.z };
                double s = dotXZ(d, rightH);    // 右方向の距離 [m]
                double t = dotXZ(d, forwardH);  // 前方向の距離 [m]

                sam.push_back({ s, t, double(h) });
            }
            if (sam.size() < 3) {
                // フォールバック：水平 & 中央高さ
                float h0 = 0.f; V3 n0;
                SampleHeightNormalBilinear(basePosWS.x, basePosWS.z, h0, &n0);
                out.pos = { basePosWS.x, h0 + baseBias, basePosWS.z };
                out.up = { 0,1,0 };
                out.forward = forwardH;
                out.right = { forwardH.z, 0.0f, -forwardH.x }; // LH: right = up × forward
                return out;
            }

            // 2) 平均中心化（y0 を分離するため）
            double ms = 0, mt = 0, my = 0;
            for (auto& p : sam) { ms += p.s; mt += p.t; my += p.y; }
            ms /= (double)sam.size(); mt /= (double)sam.size(); my /= (double)sam.size();

            double Sss = 0, Stt = 0, Sst = 0, Sy_s = 0, Sy_t = 0;
            for (auto& p : sam) {
                double s = (p.s - ms);
                double t = (p.t - mt);
                double y = (p.y - my);
                Sss += s * s;
                Stt += t * t;
                Sst += s * t;
                Sy_s += y * s;
                Sy_t += y * t;
            }

            // 3) 正規方程式
            //   [ -Sss   Sst ] [β] = [ -Sy_s ]
            //   [  Sst   Stt ] [α]   [  Sy_t ]
            double det = (-Sss) * Stt - (Sst) * (Sst);
            double alpha = 0.0, beta = 0.0; // α,β（小角, ラジアン）
            if (std::abs(det) > 1e-12) {
                alpha = ((-Sss) * Sy_t - (Sst) * (-Sy_s)) / det;
                beta = ((Sst)*Sy_t - (Stt) * (-Sy_s)) / det;
            }
            // y0：平均に戻す（中心化したので my ≈ y0 + α*mt - β*ms）
            double y0 = my - alpha * mt + beta * ms;

            // 4) 傾斜から Up を作る（平面法線）
            //   高さ関数の水平勾配 g = ∂y/∂x,∂y/∂z を yaw基底で
            //   ∂y/∂s = -β, ∂y/∂t = α なので、g_vec = (-β)*rightH + (α)*forwardH
            V3 gvec = {
                float((-beta) * rightH.x + (alpha)*forwardH.x),
                0.0f,
                float((-beta) * rightH.z + (alpha)*forwardH.z)
            };
            // 平面法線 n ∝ (-g.x, 1, -g.z)
            V3 nPlane = { -gvec.x, 1.0f, -gvec.z };
            // 上向き固定
            if (nPlane.y < 0.0f) { nPlane.x = -nPlane.x; nPlane.y = -nPlane.y; nPlane.z = -nPlane.z; }
            // 正規化
            {
                const float L = std::sqrt(nPlane.x * nPlane.x + nPlane.y * nPlane.y + nPlane.z * nPlane.z);
                if (L > 1e-9f) { nPlane.x /= L; nPlane.y /= L; nPlane.z /= L; }
                else { nPlane = upW; }
            }

            // 5) 上向きバイアス＋最大傾斜角クランプ
            auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
            auto lerp3 = [](const V3& a, const V3& b, float t) { return V3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; };
            // クランプ：upW→nPlane の角を制限
            {
                float c = std::max(-1.0f, std::min(1.0f, nPlane.x * upW.x + nPlane.y * upW.y + nPlane.z * upW.z));
                float ang = std::acos(c);
                float amax = maxTiltDeg * 3.1415926535f / 180.0f;
                if (ang > amax) {
                    // upW から nPlane へ slerp 代替（線形補間で近似）
                    float t = amax / std::max(ang, 1e-6f);
                    nPlane = lerp3(upW, nPlane, t);
                    float L = std::sqrt(nPlane.x * nPlane.x + nPlane.y * nPlane.y + nPlane.z * nPlane.z);
                    if (L > 1e-9f) { nPlane.x /= L; nPlane.y /= L; nPlane.z /= L; }
                    else nPlane = upW;
                }
            }
            // 上向きバイアス（見た目の“上に生える”を維持）
            nPlane = lerp3(nPlane, upW, clamp01(upBias));
            {
                float L = std::sqrt(nPlane.x * nPlane.x + nPlane.y * nPlane.y + nPlane.z * nPlane.z);
                if (L > 1e-9f) { nPlane.x /= L; nPlane.y /= L; nPlane.z /= L; }
                else nPlane = upW;
            }
            const V3 up = nPlane;

            // 6) forward/right を構築（LH か RH でクロス順序を切替）
            //    ここは LH（DirectX標準）版。RH の場合はコメントの行に変更。
            //    ※左手でも右手でも計算結果は同じ
            // “上り方向”をベース前方にブレンド（好みで 0.3〜0.7）
            const float uphillBlend = 0.3f;
            V3 downhill = { gvec.x, 0.0f, gvec.z }; // 勾配方向（下り）
            float dl = std::sqrt(downhill.x * downhill.x + downhill.z * downhill.z);
            if (dl > 1e-6f) { downhill.x /= dl; downhill.z /= dl; }
            else { downhill = forwardH; }
            V3 uphill = { -downhill.x, 0.0f, -downhill.z };

            V3 fwdH_mix = {
                (1.0f - uphillBlend) * forwardH.x + uphillBlend * uphill.x,
                0.0f,
                (1.0f - uphillBlend) * forwardH.z + uphillBlend * uphill.z
            };
            // 正規化
            {
                float L = std::sqrt(fwdH_mix.x * fwdH_mix.x + fwdH_mix.z * fwdH_mix.z);
                if (L > 1e-6f) { fwdH_mix.x /= L; fwdH_mix.z /= L; }
                else { fwdH_mix = forwardH; }
            }

            // --- 左手系（DirectX）の基底 ---
            V3 right = { up.z * fwdH_mix.y - up.y * fwdH_mix.z,
                         up.x * fwdH_mix.z - up.z * fwdH_mix.x,
                         up.y * fwdH_mix.x - up.x * fwdH_mix.y }; // right = up × forward  (LH)

            float rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
            if (rl > 1e-6f) { right.x /= rl; right.y /= rl; right.z /= rl; }
            else { // フォールバック
                right = { up.z, 0.0f, -up.x };
                float L = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
                if (L > 1e-6f) { right.x /= L; right.y /= L; right.z /= L; }
                else { right = { 1,0,0 }; }
            }
            V3 forward = { right.y * up.z - right.z * up.y,
                           right.z * up.x - right.x * up.z,
                           right.x * up.y - right.y * up.x }; // forward = right × up (LH)

            // RH の場合は：
            //   right   = normalize( cross(fwdH_mix, up) );
            //   forward = normalize( cross(up, right) );

            // 7) 位置Y：平面で basePos を評価
            // y = y0 + α * t0 - β * s0
            {
                // base の (s0,t0) は 0 ではない（アンカー中心と base がズレるため）
                // ただしここでは base を原点にしたので (s0,t0) = (0,0)
                // もし “アンカーの重心高さに合わせたい”なら my を使う：
                //   out.pos.y = float(my + baseBias);
                out.pos = { basePosWS.x, float(y0 + baseBias), basePosWS.z };
            }

            out.up = up; out.right = right; out.forward = forward;
            return out;
        }

        void TerrainClustered::GenerateHeights(std::vector<float>& outH,
            uint32_t vx, uint32_t vz,
            const TerrainBuildParams& p)
        {
            outH.resize(vx * vz);
            SFW::Math::Perlin2D perlin(p.seed); // ← あなたの Perlin 実装を使用（-1..1）  :contentReference[oaicite:1]{index=1}

            // fBm で -1..1 を 0..1 にマッピングしてから heightScale を掛ける
            for (uint32_t z = 0; z < vz; ++z) {
                for (uint32_t x = 0; x < vx; ++x) {
                    float nx = float(x) * p.frequency;
                    float nz = float(z) * p.frequency;
                    float h01 = 0.5f * (perlin.fbm(nx, nz, p.octaves, p.lacunarity, p.gain) + 1.0f);
                    outH[VIdx(x, z, vx)] = h01; // 0..1 の高さ（後で heightScale を掛ける）
                }
            }
        }

        void TerrainClustered::BuildVertices(std::vector<TerrainVertex>& outVtx,
            const std::vector<float>& H,
            uint32_t vx, uint32_t vz,
            float cellSize, float heightScale,
            Math::Vec3f offset)
        {
            outVtx.resize(vx * vz);

            // まず位置と UV

            for (uint32_t z = 0; z < vz; ++z) {
                for (uint32_t x = 0; x < vx; ++x) {
                    float y = H[VIdx(x, z, vx)] * heightScale;
                    TerrainVertex& v = outVtx[VIdx(x, z, vx)];
                    v.pos = make3(x * cellSize, y, z * cellSize) + offset;
                    v.uv = make2(float(x) / (vx - 1), float(z) / (vz - 1));
                    v.nrm = make3(0, 1, 0); // 後で法線を計算
                }
            }

            // 三角法線を加算 → 正規化（中央差分でも可。ここでは三角加算）
            auto addTri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
                const float3& p0 = outVtx[i0].pos;
                const float3& p1 = outVtx[i1].pos;
                const float3& p2 = outVtx[i2].pos;
                float3 n = cross(make3(p1.x - p0.x, p1.y - p0.y, p1.z - p0.z),
                    make3(p2.x - p0.x, p2.y - p0.y, p2.z - p0.z));
                outVtx[i0].nrm = n;
                outVtx[i1].nrm = n;
                outVtx[i2].nrm = n;
                };

            for (uint32_t z = 0; z < vz - 1; ++z) {
                for (uint32_t x = 0; x < vx - 1; ++x) {
                    uint32_t v00 = VIdx(x, z, vx);
                    uint32_t v10 = VIdx(x + 1, z, vx);
                    uint32_t v01 = VIdx(x, z + 1, vx);
                    uint32_t v11 = VIdx(x + 1, z + 1, vx);
                    // 2三角
                    addTri(v00, v11, v10);
                    addTri(v00, v01, v11);
                }
            }
            for (auto& v : outVtx) normalize(v.nrm);
        }

        void TerrainClustered::BuildClusters(std::vector<uint32_t>& outIndexPool,
            std::vector<ClusterRange>& outClusters,
            uint32_t& outClustersX, uint32_t& outClustersZ,
            const std::vector<float>& H,
            uint32_t cellsX, uint32_t cellsZ,
            uint32_t clusterCellsX, uint32_t clusterCellsZ,
            float cellSize, float heightScale,
            Math::Vec3f offset)
        {
            // クラスター個数
            outClustersX = (cellsX + clusterCellsX - 1) / clusterCellsX;
            outClustersZ = (cellsZ + clusterCellsZ - 1) / clusterCellsZ;
            outClusters.resize(outClustersX * outClustersZ);

            // クラスターごとにローカルなインデックス蓄積 → 後で outIndexPool に連結
            std::vector<std::vector<uint32_t>> tempIndices(outClusters.size());
            std::vector<Math::AABB3f> tempBounds(outClusters.size());

            // AABB 初期化
            constexpr auto inf = std::numeric_limits<float>::infinity();
            for (auto& b : tempBounds) {
                b.lb = make3(inf, inf, inf);
                b.ub = make3(-inf, -inf, -inf);
            }

            auto clusterId = [&](uint32_t cx, uint32_t cz) { return cz * outClustersX + cx; };

            // セルを走査して所属クラスターに 2 枚三角のインデックスを追加
            // 位置計算は後段で頂点配列にあるので、ここでは AABB 用に高さのみ参照（XZ はセル座標から計算）
            const uint32_t vx = cellsX + 1;
            const uint32_t vz = cellsZ + 1;

            for (uint32_t cz = 0; cz < outClustersZ; ++cz) {
                for (uint32_t cx = 0; cx < outClustersX; ++cx) {
                    const uint32_t id = clusterId(cx, cz);
                    // このクラスターが担当するセル範囲
                    uint32_t x0 = cx * clusterCellsX;
                    uint32_t z0 = cz * clusterCellsZ;
                    uint32_t x1 = std::min(x0 + clusterCellsX, cellsX);
                    uint32_t z1 = std::min(z0 + clusterCellsZ, cellsZ);

                    auto& indices = tempIndices[id];
                    auto& bounds = tempBounds[id];

                    // AABB をセル範囲の頂点で更新（簡便）
                    for (uint32_t z = z0; z <= z1; ++z) {
                        for (uint32_t x = x0; x <= x1; ++x) {
                            float Yw = H[VIdx(x, z, vx)] * heightScale;   // ワールド高さ
                            float Xw = float(x) * cellSize;
                            float Zw = float(z) * cellSize;
                            // XZ はワールド座標でなくセルインデックス基準。必要なら scale/offset を持ち込む
                            ExpandAABB(bounds, make3(Xw, Yw, Zw) + offset);
                        }
                    }

                    // 三角形のインデックスをローカルに詰める
                    for (uint32_t z = z0; z < z1; ++z) {
                        for (uint32_t x = x0; x < x1; ++x) {
                            uint32_t v00 = VIdx(x, z, vx);
                            uint32_t v10 = VIdx(x + 1, z, vx);
                            uint32_t v01 = VIdx(x, z + 1, vx);
                            uint32_t v11 = VIdx(x + 1, z + 1, vx);
                            // TRIANGLELIST の 2 三角（頂点プル設計だと “インデックス列” がそのまま可視列になる）
                            indices.push_back(v00); indices.push_back(v10); indices.push_back(v11);
                            indices.push_back(v00); indices.push_back(v11); indices.push_back(v01);
                        }
                    }
                }
            }

            // すべてのクラスターを連結してグローバル IndexPool を構築
            outIndexPool.clear();
            outIndexPool.reserve([&] {
                size_t sum = 0;
                for (auto& v : tempIndices) sum += v.size();
                return sum;
                }());

            uint32_t running = 0;
            for (uint32_t cz = 0; cz < outClustersZ; ++cz) {
                for (uint32_t cx = 0; cx < outClustersX; ++cx) {
                    uint32_t id = clusterId(cx, cz);
                    auto& local = tempIndices[id];

                    ClusterRange r{};
                    r.indexOffset = running;
                    r.indexCount = static_cast<uint32_t>(local.size());
                    r.bounds = tempBounds[id];

                    outClusters[id] = r;

                    // 連結コピー
                    outIndexPool.insert(outIndexPool.end(), local.begin(), local.end());
                    running += r.indexCount;
                }
            }
        }

    }
} // namespace SFW
