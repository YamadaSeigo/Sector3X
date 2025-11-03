#include "Graphics/TerrainClustered.h"

namespace SFW {

    namespace Graphics
    {
        namespace
        {
            using float3 = Math::Vec3f;
			using float2 = Math::Vec2f;
        }

        static float3 make3(float x, float y, float z) { return { x,y,z }; }
        static float2 make2(float x, float y) { return { x,y }; }

        static float3 cross(const float3& a, const float3& b) {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        }
        static void normalize(float3& v) {
            float s = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (s > 1e-20f) { v.x /= s; v.y /= s; v.z /= s; }
        }

        TerrainClustered TerrainClustered::Build(const TerrainBuildParams& p)
        {
            TerrainClustered t{};
            t.vertsX = p.cellsX + 1;
            t.vertsZ = p.cellsZ + 1;

            // 1) 高さ場
            std::vector<float> H;
            GenerateHeights(H, t.vertsX, t.vertsZ, p);

            // 2) 頂点（位置・法線・UV）
            BuildVertices(t.vertices, H, t.vertsX, t.vertsZ, p.cellSize, p.heightScale);

            // 3) クラスターを作って IndexPool に連結
            BuildClusters(t.indexPool, t.clusters, t.clustersX, t.clustersZ,
                H, p.cellsX, p.cellsZ, p.clusterCellsX, p.clusterCellsZ, p.cellSize, p.heightScale);

            return t;
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
            float cellSize, float heightScale)
        {
            outVtx.resize(vx * vz);

            // まず位置と UV

            for (uint32_t z = 0; z < vz; ++z) {
                for (uint32_t x = 0; x < vx; ++x) {
                    float y = H[VIdx(x, z, vx)] * heightScale;
                    TerrainVertex& v = outVtx[VIdx(x, z, vx)];
                    v.pos = make3(x * cellSize, y, z * cellSize);
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
                outVtx[i0].nrm = make3(outVtx[i0].nrm.x + n.x, outVtx[i0].nrm.y + n.y, outVtx[i0].nrm.z + n.z);
                outVtx[i1].nrm = make3(outVtx[i1].nrm.x + n.x, outVtx[i1].nrm.y + n.y, outVtx[i1].nrm.z + n.z);
                outVtx[i2].nrm = make3(outVtx[i2].nrm.x + n.x, outVtx[i2].nrm.y + n.y, outVtx[i2].nrm.z + n.z);
                };

            for (uint32_t z = 0; z < vz - 1; ++z) {
                for (uint32_t x = 0; x < vx - 1; ++x) {
                    uint32_t v00 = VIdx(x, z, vx);
                    uint32_t v10 = VIdx(x + 1, z, vx);
                    uint32_t v01 = VIdx(x, z + 1, vx);
                    uint32_t v11 = VIdx(x + 1, z + 1, vx);
                    // 2三角
                    addTri(v00, v10, v11);
                    addTri(v00, v11, v01);
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
            float cellSize, float heightScale)
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
                            ExpandAABB(bounds, make3(Xw, Yw, Zw));
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
