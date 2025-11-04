#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <array>
#include "../Math/Perlin2D.h" // SFW::Math::Perlin2D（-1..1）を使用
#include "../Math/AABB.hpp"

namespace SFW {

    namespace Graphics
    {

        struct TerrainVertex {
            Math::Vec3f pos;
            Math::Vec3f nrm;
            Math::Vec2f uv;
        };

        struct ClusterRange {
            uint32_t indexOffset; // IndexPool 内の開始オフセット（uint32_t 要素単位）
            uint32_t indexCount;  // ここから何個の index を読むか
            Math::AABB3f     bounds;
            // 将来 LOD を足すなら std::array<ClusterRange, MaxLod> などにする
        };

        struct TerrainBuildParams {
            uint32_t cellsX = 256;      // X 方向セル数（頂点は +1）
            uint32_t cellsZ = 256;      // Z 方向セル数
            float    cellSize = 1.0f;   // グリッド間隔
            float    heightScale = 30.f;

            // Perlin fBm
            uint32_t seed = 1337;
            int      octaves = 5;
            float    lacunarity = 2.0f;
            float    gain = 0.45f;
            float    frequency = 1.0f / 64.0f; // グリッド上でのスケール

            // クラスター設定（セル基準）
            uint32_t clusterCellsX = 32; // クラスターの幅（セル数）
            uint32_t clusterCellsZ = 32; // クラスターの高さ（セル数）
        };

        struct TerrainClustered {
            // GPU に置く想定の “頂点プール（SRV 向け SoA にしてもOK。まずは AoS）”
            std::vector<TerrainVertex> vertices;
            // 全三角形を “クラスター順に” 連結した IndexPool（TRIANGLELIST 用）
            std::vector<uint32_t>      indexPool;

            // クラスター表（IndexPool 上の範囲と AABB）
            uint32_t clustersX = 0;
            uint32_t clustersZ = 0;
            std::vector<ClusterRange> clusters; // size = clustersX * clustersZ

            // グリッド解像度（デバッグや座標変換に）
            uint32_t vertsX = 0;
            uint32_t vertsZ = 0;

            // 生成
            static TerrainClustered Build(const TerrainBuildParams& p, std::vector<float>* outMap = nullptr);

            struct HeightField {
                std::vector<float> H01; // size = (cellsX+1)*(cellsZ+1)
                uint32_t vertsX, vertsZ;
            };

            static TerrainClustered BuildFromHeightMap(const HeightField& hf, const TerrainBuildParams& p);

        private:
            static void GenerateHeights(std::vector<float>& outH,
                uint32_t vx, uint32_t vz,
                const TerrainBuildParams& p);

            static void BuildVertices(std::vector<TerrainVertex>& outVtx,
                const std::vector<float>& H,
                uint32_t vx, uint32_t vz,
                float cellSize, float heightScale);

            static void BuildClusters(std::vector<uint32_t>& outIndexPool,
                std::vector<ClusterRange>& outClusters,
                uint32_t& outClustersX, uint32_t& outClustersZ,
                const std::vector<float>& H,
                uint32_t cellsX, uint32_t cellsZ,
                uint32_t clusterCellsX, uint32_t clusterCellsZ,
                float cellSize, float heightScale);

            static inline uint32_t VIdx(uint32_t x, uint32_t z, uint32_t vx) {
                return z * vx + x;
            }

            static inline void ExpandAABB(Math::AABB3f& b, const Math::Vec3f& p) {
                b.lb.x = (std::min)(b.lb.x, p.x);
                b.lb.y = (std::min)(b.lb.y, p.y);
                b.lb.z = (std::min)(b.lb.z, p.z);
                b.ub.x = (std::max)(b.ub.x, p.x);
                b.ub.y = (std::max)(b.ub.y, p.y);
                b.ub.z = (std::max)(b.ub.z, p.z);
            }
        };
    }

} // namespace SFW
