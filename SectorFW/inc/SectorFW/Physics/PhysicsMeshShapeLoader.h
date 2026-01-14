#pragma once
#include <vector>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <optional>
#include <fstream>
#include <cstring> // std::memcmp

#include "PhysicsTypes.h"

namespace SFW::Physics
{
    struct MeshShapeData
    {
        std::vector<Vec3f> vertices;
        std::vector<std::uint32_t> indices;
    };

    /// @brief JMSH (.meshbin) を読み込む
    /// @param path             ファイルパス
    /// @param outData          頂点 / インデックス出力先
    /// @param flipRightHanded  右手系にフリップする場合 true（x を反転）
    /// @param expectedVersion  バイナリフォーマットのバージョン（デフォルト 1）
    /// @return 成功したら true
    bool LoadMeshShapeBin(
        const std::filesystem::path& path,
        MeshShapeData& outData,
        bool flipRightHanded = false,
        std::uint32_t expectedVersion = 1
    );

    /// @brief メモリ上のバッファから JMSH を読み込む版（必要なら）
    bool LoadMeshShapeBinFromMemory(
        const void* data,
        std::size_t size,
        MeshShapeData& outData,
        bool flipRightHanded = false,
        std::uint32_t expectedVersion = 1
    );
}
