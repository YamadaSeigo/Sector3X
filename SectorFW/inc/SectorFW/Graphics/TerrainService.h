#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <span>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <cmath>

// ==== ユーザー実装の数値型を使用 ====
#include "../Math/Vector.hpp"
#include "../Math/AABB.hpp"
#include "../Math/Rectangle.hpp"

namespace SMath = SFW::Math;   // 数学名前空間のショートカット

#ifndef TERRAIN_VEC3_TYPE
using TerrainVec3 = SMath::Vec3f;  // ユーザーの Vec3f
#else
using TerrainVec3 = TERRAIN_VEC3_TYPE;
#endif
#ifndef TERRAIN_AABB_TYPE
using TerrainAABB = SMath::AABB<float, TerrainVec3>;
#else
using TerrainAABB = TERRAIN_AABB_TYPE;
#endif

namespace Terrain
{
    //==============================
    // 生成パラメータ
    //==============================
    struct GridClusterParams {
        float tileMeters = 16.0f;     // クラスタの辺長（ワールド）
        uint32_t maxClusterTriangles = 1500; // 安全上限（描画クラスタ）
        bool snapToChunk = true;      // チャンク境界に吸着（論理）
    };

    struct ChunkCreateFromCgltfDesc {
        std::string gltfOrGlbPath;    // 入力ファイル
        float scale = 1.0f;           // シーンスケール
        GridClusterParams cluster;    // グリッド吸着の粒度
    };

    struct HeightGenerator {
        float (*eval)(float x, float z) = nullptr; // float h(x,z)
    };
    struct ChunkCreateProceduralDesc {
        HeightGenerator gen;
        int quadCountX = 256;
        int quadCountZ = 256;
        float grid = 1.0f;            // 地形グリッド間隔
        GridClusterParams cluster;
    };

    //==============================
    // 描画/可視判定用データ
    //==============================
    struct Quad { TerrainVec3 p0, p1, p2, p3; };

    struct ClusterDraw {
        uint32_t indexStart;  // 元IB内
        uint32_t indexCount;
        uint32_t baseVertex;  // 使わないなら0
        uint16_t materialID;  // バケツ分け用
        uint16_t flags;       // 影/アルファなど
        uint16_t lodID;       // 0=最高
        uint16_t _pad;
    };
    struct ClusterBounds {
        TerrainAABB aabb;     // 可視・Hi-Z
        float geomError;      // LOD 切替用
    };

    // SoA View（レンダラが高速に走査できるよう生ポインタを提供）
    struct ClusterBoundsSoA {
        const float* minX; const float* minY; const float* minZ;
        const float* maxX; const float* maxY; const float* maxZ;
        const float* geomError;
        size_t count;
    };
    struct ClusterDrawSoA {
        const uint32_t* indexStart; const uint32_t* indexCount; const uint32_t* baseVertex;
        const uint16_t* materialID; const uint16_t* flags; const uint16_t* lodID;
        size_t count;
    };

    struct OccluderProxy { // MOC 用（簡易版）
        TerrainAABB aabb;
        Quad frontQuad; // L0（前面クアッド）
    };

    //==============================
    // CPU メッシュ保持（バックエンド非依存）
    //==============================
    enum class IndexFormat { U16, U32 };
    struct CPUChunkMesh {
        std::vector<TerrainVec3> positions; // 位置（必要なら法線/UV を別管理）
        std::vector<uint16_t>    indices16; // どちらか一方を使用
        std::vector<uint32_t>    indices32;
        IndexFormat indexFormat = IndexFormat::U16;
        uint32_t indexCount() const {
            return indexFormat == IndexFormat::U16 ? (uint32_t)indices16.size()
                : (uint32_t)indices32.size();
        }
    };

    //==============================
    // 句柄 & ビュー
    //==============================
    using ChunkHandle = uint32_t; // 1-based

    struct ChunkView {
        ClusterBoundsSoA bounds;
        ClusterDrawSoA   draw;
        std::span<const OccluderProxy> occluders; // MOC 投入用
        const CPUChunkMesh* cpu = nullptr;        // 参照用（レンダラ側がアップロードに使う）
    };

    //==============================
    // TerrainService — 公開 API
    //==============================
    class TerrainService {
    public:
        TerrainService();
        ~TerrainService();

        // 生成
        ChunkHandle CreateChunkFromCgltf(const ChunkCreateFromCgltfDesc&);
        ChunkHandle CreateChunkProcedural(const ChunkCreateProceduralDesc&);

        // 破棄
        void DestroyChunk(ChunkHandle h);

        // 参照
        ChunkView GetChunkView(ChunkHandle h) const;

        // 可視クラスタの index 範囲を連結して **CPU メモリ**に出力（レンダラ非依存）
        struct MegaIndexStream {
            IndexFormat format = IndexFormat::U16;
            std::vector<uint16_t> u16; // format==U16
            std::vector<uint32_t> u32; // format==U32
            uint32_t size() const { return format == IndexFormat::U16 ? (uint32_t)u16.size() : (uint32_t)u32.size(); }
            void clear() { u16.clear(); u32.clear(); }
        };
        // visibleClusterIDs に従って元IBから連結コピー
        bool BuildMegaIndex(std::span<const uint32_t> visibleClusterIDs, ChunkHandle h,
            /*inout*/MegaIndexStream& out);

    private:
        struct ChunkData; // pImpl
        std::vector<std::unique_ptr<ChunkData>> m_chunks; // ハンドル = index+1

        // 内部ユーティリティ
        static Quad MakeFrontFaceQuad(const TerrainAABB& box, const TerrainVec3& camTo);
    };
}