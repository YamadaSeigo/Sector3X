#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <array>
#include <functional>

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

        struct DesignerHeightMap
        {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<float> data; // 0..1

            bool IsValid() const { return width > 0 && height > 0 && !data.empty(); }

            // u,v: 0..1 （タイル全体に対する UV）
            float Sample(float u, float v) const
            {
                if (!IsValid()) return 0.0f;

                u = std::clamp(u, 0.0f, 1.0f);
                v = std::clamp(v, 0.0f, 1.0f);

                float x = u * (width - 1);
                float y = v * (height - 1);

                int x0 = (int)std::floor(x);
                int y0 = (int)std::floor(y);
                int x1 = (std::min)(x0 + 1, (int)width - 1);
                int y1 = (std::min)(y0 + 1, (int)height - 1);

                float tx = x - (float)x0;
                float ty = y - (float)y0;

                float h00 = data[y0 * width + x0];
                float h10 = data[y0 * width + x1];
                float h01 = data[y1 * width + x0];
                float h11 = data[y1 * width + x1];

                float hx0 = std::lerp(h00, h10, tx);
                float hx1 = std::lerp(h01, h11, tx);
                return std::lerp(hx0, hx1, ty); // 0..1
            }
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

			Math::Vec3f offset = Math::Vec3f(0.0f, 0.0f, 0.0f); // ワールドオフセット

            const DesignerHeightMap* designer = nullptr;
        };



        struct TerrainClustered {

            struct ClusterRange {
                uint32_t indexOffset = {}; // IndexPool 内の開始オフセット（uint32_t 要素単位）
                uint32_t indexCount = {};  // ここから何個の index を読むか
                Math::AABB3f     bounds = {};
                // 将来 LOD を足すなら std::array<ClusterRange, MaxLod> などにする
            };

            // ---- Splat 用メタ（レンダラー非依存）----
            static constexpr uint32_t kSplatMaxLayers = 4;

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

            static void WeldVerticesAlongBorders(std::vector<SFW::Graphics::TerrainVertex>& vertices,
                std::vector<uint32_t>& indexPool,
                float cellSize);

            static bool CheckClusterBorderEquality(const std::vector<uint32_t>& indexPool,
                const std::vector<TerrainClustered::ClusterRange>& clusters,
                uint32_t clustersX, uint32_t clustersZ);

            // クラスタ4辺に“下向きスカート”を常設する（BuildClustersの直後、LOD生成の前に一度だけ呼ぶ）
            static void AddSkirtsToClusters(SFW::Graphics::TerrainClustered& t,
                float skirtDepth /*= 0.2f など*/);


            struct SplatLayerMeta {
                uint32_t materialId = 0;     // 素材の論理ID（DX11側でSRVに解決）
                float    uvTilingU = 1.0f;   // レイヤ毎タイル
                float    uvTilingV = 1.0f;
            };

            struct ClusterSplatMeta {
                uint32_t layerCount = 4;                         // 0..4
                SplatLayerMeta layers[kSplatMaxLayers];          // 4レイヤ
                uint32_t splatTextureId = 0;                     // RGBA ウェイトを持つスプラットテクスチャの論理ID
                float    splatUVScaleU = 1.0f, splatUVScaleV = 1.0f; // スプラットUVタイル（頂点uvに乗算）
                float    splatUVOffsetU = 0.0f, splatUVOffsetV = 0.0f;
            };

            std::vector<ClusterSplatMeta> splat; // size == clusters.size()

            void InitSplatDefault(uint32_t commonSplatTexId,
                const uint32_t materialIds[4],
                const float tilingUV[4][2],
                float splatScaleU = 1.0f, float splatScaleV = 1.0f,
                float splatOffsetU = 0.0f, float splatOffsetV = 0.0f);

            // 生成関数版：クラスターごとに好きなロジックで決める
            using SplatGenerator = std::function<ClusterSplatMeta(uint32_t cid, const ClusterRange& c)>;
            void InitSplatWithGenerator(const SplatGenerator& gen);

            static inline uint32_t VIdx(uint32_t x, uint32_t z, uint32_t vx) {
                return z * vx + x;
            }

            struct RigidPose {
                Math::Vec3f pos;      // 位置（WS）
                Math::Vec3f right;    // 基底X（WS）
                Math::Vec3f up;       // 基底Y（WS）
                Math::Vec3f forward;  // 基底Z（WS）
            };

            // 地形サンプル：ワールドXZから高さ/法線（バイリニア）を取得
            bool SampleHeightNormalBilinear(float x, float z, float& outH, Math::Vec3f* outN = nullptr) const;

            // アンカーで“底面”を合わせて自然な回転を返す
            RigidPose SolvePlacementByAnchors(
                const Math::Vec3f& basePosWS,    // 配置の基準（x,z使用、yは無視して地形に合わせる）
                float yawRad,                    // 事前のヨー回転（上から見た回転）
                float scale,                     // インスタンスのスケール（XZに適用）
                const std::vector<Math::Vec2f>& anchorsLocalXZ, // 底面アンカー（ローカルXZ）
                float maxTiltDeg = 15.0f,        // 上限傾斜（横倒れ防止）
                float upBias = 0.5f,             // 上向きバイアス（0=法線寄り, 1=より上向き）
                float baseBias = 0.01f           // 微小押し上げ（めり込み防止, m単位）
            ) const;
        private:
            static void GenerateHeightsOnlyPerlin(std::vector<float>& outH,
                uint32_t vx, uint32_t vz,
                const TerrainBuildParams& p);

            static void GenerateHeights(std::vector<float>& outH,
                uint32_t vx, uint32_t vz,
                const TerrainBuildParams& p);

            static void BuildVertices(std::vector<TerrainVertex>& outVtx,
                const std::vector<float>& H,
                uint32_t vx, uint32_t vz,
                float cellSize, float heightScale,
                Math::Vec3f offset = { 0,0,0 });

            static void BuildClusters(std::vector<uint32_t>& outIndexPool,
                std::vector<ClusterRange>& outClusters,
                uint32_t& outClustersX, uint32_t& outClustersZ,
                const std::vector<float>& H,
                uint32_t cellsX, uint32_t cellsZ,
                uint32_t clusterCellsX, uint32_t clusterCellsZ,
                float cellSize, float heightScale,
                Math::Vec3f offset = { 0,0,0 });
        };
    }
} // namespace SFW
