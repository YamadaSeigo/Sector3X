#include "Physics/PhysicsMeshShapeLoader.h"

namespace SFW::Physics
{
    namespace
    {
        // 安全な読み取り用の簡単なビュー
        struct BinaryReader
        {
            const std::uint8_t* cur = nullptr;
            const std::uint8_t* end = nullptr;

            BinaryReader(const void* data, std::size_t size)
            {
                cur = static_cast<const std::uint8_t*>(data);
                end = cur + size;
            }

            bool readBytes(void* dst, std::size_t size)
            {
                if (cur + size > end) return false;
                std::memcpy(dst, cur, size);
                cur += size;
                return true;
            }

            template <class T>
            bool read(T& out)
            {
                return readBytes(&out, sizeof(T));
            }

            std::size_t remaining() const { return static_cast<std::size_t>(end - cur); }
        };
    } // anonymous namespace

    bool LoadMeshShapeBinFromMemory(
        const void* data,
        std::size_t size,
        MeshShapeData& outData,
        bool flipRightHanded,
        std::uint32_t expectedVersion
    )
    {
        outData.vertices.clear();
        outData.indices.clear();

        if (!data || size < 4 + 4 + 4 + 4)
        {
            // 最低限のヘッダもない
            return false;
        }

        BinaryReader br(data, size);

        char magic[4];
        if (!br.readBytes(magic, sizeof(magic)))
            return false;

        if (std::memcmp(magic, "JMSH", 4) != 0)
        {
            // 異なるフォーマット
            return false;
        }

        std::uint32_t version = 0;
        if (!br.read(version))
            return false;

        if (version != expectedVersion)
        {
            // バイナリフォーマットのバージョン違い
            return false;
        }

        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
        if (!br.read(vertexCount) || !br.read(indexCount))
            return false;

        // ざっくり安全チェック（サイズオーバー防止）
        const std::size_t minRequired =
            sizeof(float) * 3ull * vertexCount + // Vec3 * vertexCount
            sizeof(std::uint32_t) * indexCount;

        if (br.remaining() < minRequired)
        {
            // ファイルサイズが足りない
            return false;
        }

        outData.vertices.resize(vertexCount);
        outData.indices.resize(indexCount);

        // 頂点読み取り
        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            float xyz[3];
            if (!br.readBytes(xyz, sizeof(xyz)))
                return false;

            Vec3f v;
            v.x = xyz[0];
            v.y = xyz[1];
            v.z = xyz[2];

            if (flipRightHanded)
            {
                v.x = -v.x; // 右手系フリップ: X を反転
            }

            outData.vertices[i] = v;
        }

        // インデックス読み取り
        if (!br.readBytes(outData.indices.data(), sizeof(std::uint32_t) * indexCount))
            return false;

        return true;
    }

    bool LoadMeshShapeBin(
        const std::filesystem::path& path,
        MeshShapeData& outData,
        bool flipRightHanded,
        std::uint32_t expectedVersion
    )
    {
        // ファイル全体を読み込んでからメモリ版に渡す
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
        {
            return false;
        }

        ifs.seekg(0, std::ios::end);
        std::streampos len = ifs.tellg();
        if (len <= 0)
        {
            return false;
        }

        ifs.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(len));
        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), len))
        {
            return false;
        }

        return LoadMeshShapeBinFromMemory(
            buffer.data(), buffer.size(),
            outData,
            flipRightHanded,
            expectedVersion
        );
    }
}
