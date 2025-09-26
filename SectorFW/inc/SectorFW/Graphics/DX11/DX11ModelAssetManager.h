/*****************************************************************//**
 * @file   DX11ModelAssetManager.h
 * @brief DirectX 11用のモデルアセットマネージャーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <filesystem>

#include "DX11MeshManager.h"

#include "../../Math/Matrix.hpp"
#include "../../Util/PathView.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		struct SkeletonJoint {
			std::string name;
			int parentIndex = -1; // -1 なら root
			Math::Matrix4x4f inverseBindMatrix = {};
		};

		struct Skeleton {
			std::vector<SkeletonJoint> joints;
		};

		struct DX11ModelAssetCreateDesc {
			std::string path;
			ShaderHandle shader = {};
			PSOHandle pso = {};
			bool rhFlipZ = false; // Z軸反転フラグ（右手系GLTF用）
		};

		struct DX11ModelAssetData {
			std::string name;

			struct SubMesh {
				MeshHandle mesh;
				MaterialHandle material;
				PSOHandle pso;
				bool hasInstanceData = false;
				InstanceData instance;
			};

			std::vector<SubMesh> subMeshes = {};

			std::optional<Skeleton> skeleton; // スケルトンがある場合
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
				DX11BufferManager& cbManager,
				DX11SamplerManager& samplerManager,
				ID3D11Device* device);

			std::optional<ModelAssetHandle> FindExisting(const DX11ModelAssetCreateDesc& d) {
				auto p = std::filesystem::weakly_canonical(d.path);
				if (auto it = pathToHandle.find(p); it != pathToHandle.end()) return it->second;
				return std::nullopt;
			}
			void RegisterKey(const DX11ModelAssetCreateDesc& d, ModelAssetHandle h) {
				pathToHandle.emplace(std::filesystem::weakly_canonical(d.path), h);
			}

			DX11ModelAssetData CreateResource(const DX11ModelAssetCreateDesc& desc, ModelAssetHandle h) {
				return LoadFromGLTF(desc.path, desc.shader, desc.pso, desc.rhFlipZ);
			}

			void RemoveFromCaches(uint32_t idx);
			void DestroyResource(uint32_t idx, uint64_t currentFrame);

			// キャッシュ対応：内部で保持して返す
			const DX11ModelAssetData LoadFromGLTF(
				const std::string& path,
				ShaderHandle shader,
				PSOHandle pso,
				bool flipZ = false);
		private:
			DX11MeshManager& meshMgr;
			DX11MaterialManager& matMgr;
			DX11ShaderManager& shaderMgr;
			DX11TextureManager& texMgr;
			DX11BufferManager& cbManager;
			DX11SamplerManager& samplerManager;
			ID3D11Device* device;

			// キャッシュ：正規化パス→ModelAsset
			std::unordered_map<std::filesystem::path, ModelAssetHandle> pathToHandle;
		};
	}
}
