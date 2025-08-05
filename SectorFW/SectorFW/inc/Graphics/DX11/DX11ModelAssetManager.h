#pragma once

#include <filesystem>

#include "DX11MeshManager.h"

#include "Math/Matrix.hpp"
#include "Util/PathView.hpp"

namespace SectorFW
{
	namespace Graphics
	{
        struct SkeletonJoint {
            std::string name;
            int parentIndex = -1; // -1 なら root
            Math::Matrix4x4f inverseBindMatrix;
        };

        struct Skeleton {
            std::vector<SkeletonJoint> joints;
        };

        struct DX11ModelAssetCreateDesc {
            std::string path;
            ShaderHandle shader = {};
            PSOHandle pso = {};
        };

        struct DX11ModelAssetData {
            std::string name;

            struct SubMesh {
                MeshHandle mesh;
                MaterialHandle material;
                PSOHandle pso;
                InstanceData instance;
            };

            std::vector<SubMesh> subMeshes = {};

			std::optional<Skeleton> skeleton; // スケルトンがある場合

            std::vector<DrawCommand> ToDrawCommands() const {
                std::vector<DrawCommand> cmds;
                for (const auto& sm : subMeshes)
                    cmds.emplace_back(sm.mesh, sm.material, sm.pso, sm.instance);
                return cmds;
            }
        private:
			path_view path; // キャッシュ用のパスビュー

			friend class DX11ModelAssetManager;
        };

        class DX11ModelAssetManager : public
            ResourceManagerBase<DX11ModelAssetManager, ModelAssetHandle, DX11ModelAssetCreateDesc, DX11ModelAssetData> {
        public:
            DX11ModelAssetManager(
                DX11MeshManager& meshMgr,
                DX11MaterialManager& matMgr,
                DX11ShaderManager& shaderMgr,
                DX11TextureManager& texMgr,
                ID3D11Device* device);

			DX11ModelAssetData CreateResource(const DX11ModelAssetCreateDesc& desc) {
				return LoadFromGLTF(desc.path, desc.shader, desc.pso);
			}

            void ScheduleDestroy(uint32_t idx, uint64_t deleteFrame);

            void ProcessDeferredDeletes(uint64_t currentFrame);

            // キャッシュ対応：内部で保持して返す
            const DX11ModelAssetData& LoadFromGLTF(
                const std::string& path,
                ShaderHandle shader,
                PSOHandle pso);
        private:
            DX11MeshManager& meshMgr;
            DX11MaterialManager& matMgr;
            DX11ShaderManager& shaderMgr;
            DX11TextureManager& texMgr;
            ID3D11Device* device;

            // キャッシュ：正規化パス→ModelAsset
            std::unordered_map<std::filesystem::path, DX11ModelAssetData> assetCache;
            std::mutex cacheMutex;

            struct PendingDelete {
                uint32_t index;
                uint64_t deleteSync;
            };
            std::vector<PendingDelete> pendingDelete;
        };
	}
}
