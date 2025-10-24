#include "Graphics/TerrainService.h"
#include <cassert>
#include <cstring>

using namespace Terrain;

//==============================
// 内部構造体（pImpl）
//==============================
struct Terrain::TerrainService::ChunkData {
    CPUChunkMesh mesh;                             // CPU メッシュ
    std::vector<ClusterBounds> clustersBounds;     // AoS（SoA View で参照）
    std::vector<ClusterDraw>   clustersDraw;
    std::vector<OccluderProxy> occluders;          // MOC 用（前面クアッド）
    TerrainAABB worldAabb{};
    float tileMeters = 16.0f;
};

TerrainService::TerrainService() {}
TerrainService::~TerrainService() {}

// 数学ユーティリティ
static inline float Dot(const TerrainVec3& a, const TerrainVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline TerrainVec3  Normalize(const TerrainVec3& v) { float l = std::sqrt(Dot(v, v)); return (l > 0) ? TerrainVec3{ v.x / l,v.y / l,v.z / l } : TerrainVec3{ 0,0,0 }; }
static inline TerrainVec3  MinV(const TerrainVec3& a, const TerrainVec3& b) { return { std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z) }; }
static inline TerrainVec3  MaxV(const TerrainVec3& a, const TerrainVec3& b) { return { std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z) }; }

Terrain::Quad TerrainService::MakeFrontFaceQuad(const TerrainAABB& box, const TerrainVec3& camTo)
{
    TerrainVec3 c{ (box.lb.x + box.ub.x) * 0.5f, (box.lb.y + box.ub.y) * 0.5f, (box.lb.z + box.ub.z) * 0.5f };
    TerrainVec3 ext{ (box.ub.x - box.lb.x) * 0.5f, (box.ub.y - box.lb.y) * 0.5f, (box.ub.z - box.lb.z) * 0.5f };
    TerrainVec3 toCam = Normalize(camTo);
    struct Face { TerrainVec3 n; Quad q; };
    const float x0 = c.x - ext.x, x1 = c.x + ext.x;
    const float y0 = c.y - ext.y, y1 = c.y + ext.y;
    const float z0 = c.z - ext.z, z1 = c.z + ext.z;
    Face faces[6] = {
        { {+1,0,0}, {{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}} }, // +X
        { {-1,0,0}, {{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}} }, // -X
        { {0,+1,0}, {{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}} }, // +Y
        { {0,-1,0}, {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}} }, // -Y
        { {0,0,+1}, {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}} }, // +Z
        { {0,0,-1}, {{x0,y1,z0},{x1,y1,z0},{x1,y0,z0},{x0,y0,z0}} }, // -Z
    };
    int best = 0; float bestDot = -1e9f;
    for (int i = 0; i < 6; i++) { float d = Dot(faces[i].n, toCam); if (d > bestDot) { bestDot = d; best = i; } }
    return faces[best].q;
}

static void ComputeAABB(const std::vector<TerrainVec3>& pos, TerrainAABB& out)
{
    if (pos.empty()) { out.lb = { 0,0,0 }; out.ub = { 0,0,0 }; return; }
    TerrainVec3 lo = pos[0], hi = pos[0];
    for (auto& p : pos) { lo = MinV(lo, p); hi = MaxV(hi, p); } out.lb = lo; out.ub = hi;
}

static void GridClusterize(const std::vector<TerrainVec3>& positions,
    const std::vector<uint32_t>& indices32,
    const std::vector<uint16_t>& materialOfTri,
    float tileMeters,
    /*out*/std::vector<ClusterBounds>& outB,
    /*out*/std::vector<ClusterDraw>& outD,
    /*out*/std::vector<uint16_t>& outIB16,
    /*out*/std::vector<uint32_t>& outIB32,
    /*out*/std::vector<OccluderProxy>& outOcc)
{
    TerrainAABB world{}; ComputeAABB(positions, world);
    struct Cell { std::vector<uint32_t> tris; };
    std::unordered_map<int64_t, Cell> cells; cells.reserve(indices32.size() / 3);
    auto keyOf = [&](const TerrainVec3& p) {
        int64_t gx = (int64_t)std::floor((p.x - world.lb.x) / tileMeters);
        int64_t gz = (int64_t)std::floor((p.z - world.lb.z) / tileMeters);
        return (gx << 32) ^ (gz & 0xffffffff);
        };
    for (size_t t = 0; t < indices32.size(); t += 3) {
        TerrainVec3 p0 = positions[indices32[t + 0]];
        TerrainVec3 p1 = positions[indices32[t + 1]];
        TerrainVec3 p2 = positions[indices32[t + 2]];
        TerrainVec3 c{ (p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f, (p0.z + p1.z + p2.z) / 3.0f };
        int64_t k = keyOf(c);
        cells[k].tris.push_back((uint32_t)t);
    }

    outB.clear(); outD.clear(); outIB16.clear(); outIB32.clear(); outOcc.clear();

    for (auto& [k, cell] : cells) {
        if (cell.tris.empty()) continue;
        std::unordered_map<uint16_t, std::vector<uint32_t>> byMat;
        for (uint32_t tIndex : cell.tris) {
            uint16_t mat = materialOfTri[tIndex / 3];
            byMat[mat].push_back(tIndex);
        }
        for (auto& [mat, triList] : byMat) {
            TerrainAABB aabb{ { +1e9f,+1e9f,+1e9f }, { -1e9f,-1e9f,-1e9f } };
            size_t idxStart16 = outIB16.size();
            size_t idxStart32 = outIB32.size();
            bool need32 = false;
            for (uint32_t tIndex : triList) {
                uint32_t i0 = indices32[tIndex + 0], i1 = indices32[tIndex + 1], i2 = indices32[tIndex + 2];
                aabb.lb = MinV(aabb.lb, positions[i0]); aabb.ub = MaxV(aabb.ub, positions[i0]);
                aabb.lb = MinV(aabb.lb, positions[i1]); aabb.ub = MaxV(aabb.ub, positions[i1]);
                aabb.lb = MinV(aabb.lb, positions[i2]); aabb.ub = MaxV(aabb.ub, positions[i2]);
                if (i0 > 0xffff || i1 > 0xffff || i2 > 0xffff) need32 = true;
                outIB16.push_back((uint16_t)i0); outIB16.push_back((uint16_t)i1); outIB16.push_back((uint16_t)i2);
                outIB32.push_back(i0); outIB32.push_back(i1); outIB32.push_back(i2);
            }
            ClusterBounds cb{ aabb, /*geomError=*/0.0f };
            ClusterDraw   cd{ (uint32_t)(need32 ? idxStart32 : idxStart16), (uint32_t)(triList.size() * 3), 0u, mat, 0u, 0u, 0u };
            outB.push_back(cb); outD.push_back(cd);
            OccluderProxy occ; occ.aabb = aabb; occ.frontQuad = { aabb.lb, TerrainVec3{aabb.ub.x,aabb.lb.y,aabb.lb.z}, aabb.ub, TerrainVec3{aabb.lb.x,aabb.ub.y,aabb.ub.z} };
            outOcc.push_back(occ);
        }
    }
}

Terrain::ChunkView TerrainService::GetChunkView(ChunkHandle h) const
{
    assert(h > 0 && h <= m_chunks.size());
    const ChunkData* cd = m_chunks[h - 1].get();

    static thread_local std::vector<float> s_minX, s_minY, s_minZ, s_maxX, s_maxY, s_maxZ, s_geom;
    static thread_local std::vector<uint32_t> s_is, s_ic, s_bv;
    static thread_local std::vector<uint16_t> s_mat, s_flags, s_lod;

    size_t n = cd->clustersBounds.size();
    s_minX.resize(n); s_minY.resize(n); s_minZ.resize(n);
    s_maxX.resize(n); s_maxY.resize(n); s_maxZ.resize(n); s_geom.resize(n);
    for (size_t i = 0; i < n; i++) {
        const auto& b = cd->clustersBounds[i].aabb;
        s_minX[i] = b.lb.x; s_minY[i] = b.lb.y; s_minZ[i] = b.lb.z;
        s_maxX[i] = b.ub.x; s_maxY[i] = b.ub.y; s_maxZ[i] = b.ub.z;
        s_geom[i] = cd->clustersBounds[i].geomError;
    }
    s_is.resize(n); s_ic.resize(n); s_bv.resize(n); s_mat.resize(n); s_flags.resize(n); s_lod.resize(n);
    for (size_t i = 0; i < n; i++) {
        const auto& d = cd->clustersDraw[i];
        s_is[i] = d.indexStart; s_ic[i] = d.indexCount; s_bv[i] = d.baseVertex; s_mat[i] = d.materialID; s_flags[i] = d.flags; s_lod[i] = d.lodID;
    }

    ChunkView v{};
    v.bounds = { s_minX.data(), s_minY.data(), s_minZ.data(), s_maxX.data(), s_maxY.data(), s_maxZ.data(), s_geom.data(), n };
    v.draw = { s_is.data(), s_ic.data(), s_bv.data(), s_mat.data(), s_flags.data(), s_lod.data(), n };
    v.occluders = cd->occluders;
    v.cpu = &cd->mesh;
    return v;
}

bool TerrainService::BuildMegaIndex(std::span<const uint32_t> visibleClusterIDs, ChunkHandle h,
    /*inout*/MegaIndexStream& out)
{
    if (h == 0 || h > m_chunks.size() || !m_chunks[h - 1]) return false;
    auto* cd = m_chunks[h - 1].get();

    const bool u16 = (cd->mesh.indexFormat == IndexFormat::U16);
    if (u16) { out.format = IndexFormat::U16; out.u16.clear(); }
    else { out.format = IndexFormat::U32; out.u32.clear(); }

    for (uint32_t cid : visibleClusterIDs) {
        if (cid >= cd->clustersDraw.size()) continue;
        const auto& d = cd->clustersDraw[cid];
        if (u16) {
            const uint16_t* src = cd->mesh.indices16.data() + d.indexStart;
            out.u16.insert(out.u16.end(), src, src + d.indexCount);
        }
        else {
            const uint32_t* src = cd->mesh.indices32.data() + d.indexStart;
            out.u32.insert(out.u32.end(), src, src + d.indexCount);
        }
    }
    return true;
}

ChunkHandle TerrainService::CreateChunkFromCgltf(const ChunkCreateFromCgltfDesc& desc)
{
    auto cd = std::make_unique<ChunkData>();

    // --- ここで cgltf を用いて positions/indices/materialOfTri を取得してください ---
    const int N = 64; const float s = desc.cluster.tileMeters * 0.5f;
    cd->mesh.positions.reserve((N + 1) * (N + 1));
    for (int z = 0; z <= N; ++z) { for (int x = 0; x <= N; ++x) { cd->mesh.positions.push_back({ (x - N / 2) * s * 0.5f, 0.0f, (z - N / 2) * s * 0.5f }); } }
    std::vector<uint32_t> indices32; indices32.reserve(N * N * 6);
    std::vector<uint16_t> matOfTri;  matOfTri.reserve(N * N);
    auto idx = [&](int x, int z) {return (uint32_t)(z * (N + 1) + x); };
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            uint32_t i0 = idx(x, z), i1 = idx(x + 1, z), i2 = idx(x + 1, z + 1), i3 = idx(x, z + 1);
            indices32.insert(indices32.end(), { i0,i1,i2,  i0,i2,i3 });
            matOfTri.push_back(0);
        }
    }

    GridClusterize(cd->mesh.positions, indices32, matOfTri, desc.cluster.tileMeters,
        cd->clustersBounds, cd->clustersDraw, cd->mesh.indices16, cd->mesh.indices32, cd->occluders);

    cd->mesh.indexFormat = cd->mesh.indices16.empty() ? IndexFormat::U32 : IndexFormat::U16;
    m_chunks.push_back(std::move(cd));
    return (ChunkHandle)m_chunks.size();
}

ChunkHandle TerrainService::CreateChunkProcedural(const ChunkCreateProceduralDesc& desc)
{
    auto cd = std::make_unique<ChunkData>();

    const int NX = desc.quadCountX; const int NZ = desc.quadCountZ; const float g = desc.grid;
    cd->mesh.positions.reserve((NX + 1) * (NZ + 1));
    for (int z = 0; z <= NZ; ++z) {
        for (int x = 0; x <= NX; ++x) {
            float wx = (x - NX * 0.5f) * g;
            float wz = (z - NZ * 0.5f) * g;
            float wy = desc.gen.eval ? desc.gen.eval(wx, wz) : 0.0f;
            cd->mesh.positions.push_back({ wx,wy,wz });
        }
    }

    auto idx = [&](int x, int z) {return (uint32_t)(z * (NX + 1) + x); };
    std::vector<uint32_t> indices32; indices32.reserve(NX * NZ * 6);
    std::vector<uint16_t> matOfTri;  matOfTri.reserve(NX * NZ);
    for (int z = 0; z < NZ; ++z) {
        for (int x = 0; x < NX; ++x) {
            uint32_t i0 = idx(x, z), i1 = idx(x + 1, z), i2 = idx(x + 1, z + 1), i3 = idx(x, z + 1);
            indices32.insert(indices32.end(), { i0,i1,i2,  i0,i2,i3 });
            matOfTri.push_back(0);
        }
    }

    GridClusterize(cd->mesh.positions, indices32, matOfTri, desc.cluster.tileMeters,
        cd->clustersBounds, cd->clustersDraw, cd->mesh.indices16, cd->mesh.indices32, cd->occluders);
    cd->mesh.indexFormat = cd->mesh.indices16.empty() ? IndexFormat::U32 : IndexFormat::U16;

    m_chunks.push_back(std::move(cd));
    return (ChunkHandle)m_chunks.size();
}


