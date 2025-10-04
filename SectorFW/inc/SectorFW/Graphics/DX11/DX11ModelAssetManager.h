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
#include "../../Math/BoundingSphere.hpp"
#include "../../Math/AABB.hpp"
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
			uint32_t option = 1; // トポロジーを保持するか（LOD生成時にメッシュ最適化を行わない）
			uint32_t    instancesPeak = 1;   // 同時表示おおよそ
			float  viewMin = 0, viewMax = 100;// 想定視距離[m]
			bool hero = false;            // 注視対象（品質優先）
			bool rhFlipZ = false; // Z軸反転フラグ（右手系GLTF用）
		};

		struct LodThresholds {
			// thresholds[i] を超えると「より高品質側」を選ぶ想定（sは画面高さ比0..1）
			// 例: s > T[0] → LOD0,  T[1] < s ≤ T[0] → LOD1, ...
			std::array<float, 4> T;     // 使うのは lodCount-1 個
			float hysteresisUp = 0.15f; // 15% 幅：粗→細へ上がる時に厳しめ
			float hysteresisDown = 0.01f; // 細→粗へ下がる時に甘め
		};

		struct DX11ModelAssetData {
			std::string name;

			struct SubmeshLOD {
				MeshHandle mesh = {};                // このLODのメッシュ（VB/IB）
				std::vector<DX11MeshManager::ClusterInfo> clusters; // このLODのクラスタ（meshlets）
			};

			struct SubMesh {
				MeshHandle proxy = {};
				Math::BoundingSpheref bs = {};
				Math::AABB3f aabb = {};
				MaterialHandle material = {};
				std::vector<SubmeshLOD> lods;   // LOD0..N-1
				LodThresholds lodThresholds = {}; // LOD選択用の閾値
				PSOHandle pso = {};
				InstanceData instance = {};
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
				return LoadFromGLTF(desc);
			}

			void RemoveFromCaches(uint32_t idx);
			void DestroyResource(uint32_t idx, uint64_t currentFrame);

			// キャッシュ対応：内部で保持して返す
			const DX11ModelAssetData LoadFromGLTF(const DX11ModelAssetCreateDesc& desc);

			// ==== LOD プリセット ====
			enum class LodQualityMode : uint8_t { Attributes, Permissive, Sloppy };

			struct AssetStats {
				uint32_t    vertices;        // 代表LOD0の頂点数 or 三角数でもOK
				uint32_t    instancesPeak;   // 同時表示おおよそ
				float  viewMin, viewMax;// 想定視距離[m]
				bool   skinned;         // スキン/アニメ
				bool   alphaCutout;     // 葉/フェンス等（overdraw対策優先）
				bool   hero;            // 注視対象（品質優先）
			};

			struct LodRecipe {
				LodQualityMode mode;
				float  targetRatio = 1.0f;     // 0..1 (三角形比率)
				float  targetError = 0.0f;     // FLT_MAX で誤差制限なし
				float  wNormal = 0.8f;  // Attributes/Permissive 時: 属性重み
				float  wUV = 0.5f;
			};

			inline std::array<LodRecipe, 3> MakeDefaultLodRecipes() {
				// L0は“そのまま”なので生成しない想定。ここは L1/L2/L3 用
				return { {
					{ LodQualityMode::Attributes,  0.65f, 1e-3f, 0.9f, 0.6f }, // 近距離
					{ LodQualityMode::Permissive,  0.35f, FLT_MAX, 0.6f, 0.3f }, // 中距離
					{ LodQualityMode::Sloppy,      0.12f, FLT_MAX }              // 遠距離/背景
				} };
			}

			// LODを1段生成して MeshHandle を返す（indices/streams は L0 の SoA を入力）
			// - outIdx/outStreams は登録に使う最終 SoA/IB（必要なら呼び出し側で再利用可）
			// - 失敗時 false（フォールバックは内部でやる）
			bool BuildOneLodMesh(
				const std::vector<uint32_t>& baseIndices,
				const std::vector<SectorFW::Math::Vec3f>& basePositions,
				const std::vector<SectorFW::Math::Vec3f>* baseNormals,
				const std::vector<SectorFW::Math::Vec4f>* baseTangents,
				const std::vector<SectorFW::Math::Vec2f>* baseUV0,
				const std::vector<std::array<uint8_t, 4>>* baseSkinIdx,
				const std::vector<std::array<uint8_t, 4>>* baseSkinWgt,
				const LodRecipe& recipe,
				DX11MeshManager& meshMgr,
				const std::wstring& tagForCaching,    // 例: path + L"#subX-lodY"
				DX11ModelAssetData::SubmeshLOD& outMesh,
				std::vector<uint32_t>& outIdx,
				DX11MeshManager::RemappedStreams& outStreams,
				bool buildClusters = false);

			static int SelectLod(float s, const LodThresholds& th, int lodCount, int prevLod, float globalBias /*±段*/);

			// 返すのは LOD1..N 用のレシピ（LOD0は常に原型）
			static std::vector<LodRecipe> BuildLodRecipes(const AssetStats& a);

			static LodThresholds BuildLodThresholds(const AssetStats& a, int lodCount);

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
