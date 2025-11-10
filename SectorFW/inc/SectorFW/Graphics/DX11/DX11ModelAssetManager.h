/*****************************************************************//**
 * @file   DX11ModelAssetManager.h
 * @brief DirectX 11用のモデルアセットマネージャーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <filesystem>

#include "DX11MeshManager.h"
#include "DX11PSOManager.h"

#include "../LODPolicy.h"

#include "../../Math/Matrix.hpp"
#include "../../Math/BoundingSphere.hpp"
#include "../../Math/AABB.hpp"
#include "../../Util/PathView.hpp"


namespace SFW
{
	namespace Graphics::DX11
	{
		struct SkeletonJoint {
			std::string name;
			int parentIndex = -1; // -1 なら root
			Math::Matrix4x4f inverseBindMatrix = {};
		};

		struct Skeleton {
			std::vector<SkeletonJoint> joints;
		};

		struct ModelAssetCreateDesc {
			std::string path;
			PSOHandle pso = {};
			uint32_t option = 1; // トポロジーを保持するか（LOD生成時にメッシュ最適化を行わない）
			uint32_t    instancesPeak = 1;   // 同時表示おおよそ
			float  viewMin = 0, viewMax = 100;// 想定視距離[m]
			bool hero = false;            // 注視対象（品質優先）
			bool rhFlipZ = false; // Z軸反転フラグ（右手系GLTF用）

			// ===== Occluder 構築設定 =====
			bool  buildOccluders = true;    // インポート時にOccluder適性判定＋melt AABB生成を行う
			float occScoreThreshold = 0.5f;// このスコア以上ならOccluder化（0..1）
			int   meltResolution = 16;      // meltのボクセル解像度。小さくするほどボクセルが大きくなる（64 or 96 程度が実用。性能と品質のトレードオフ）
			float meltStopRatio = 0.3f;   // meltの停止しきい（小AABB生成を抑える目安。0.1～0.7）
			float minWorldSizeM = 1.0f;    // 小さすぎるモデルはOccluder対象外（対角長[m]）
			float minThicknessRatio = 0.01f;// 最小厚み比。これ未満は超薄板として減点
		};

		using LodThresholds = SFW::Graphics::LodThresholdsPx; // ピクセル基準

		struct ModelAssetData {
			std::string name;

			struct SubmeshLOD {
				MeshHandle mesh = {};                // このLODのメッシュ（VB/IB）
				std::vector<MeshManager::ClusterInfo> clusters; // このLODのクラスタ（meshlets）
			};

			struct SubMesh {
				Math::BoundingSpheref bs = {};
				Math::AABB3f aabb = {};
				MaterialHandle material = {};
				std::vector<SubmeshLOD> lods;   // LOD0..N-1
				LodThresholds lodThresholds = {}; // LOD選択用の閾値
				PSOHandle pso = {};
				InstanceData instance = {};

				// ===== Occluder 情報 =====
				struct OccluderInfo {
					bool  candidate = false;     // Occluder候補か
					float score = 0.0f;       // 0..1
					uint32_t estimatedAABBCount = 0; // melt見積 or 生成後の個数
					std::vector<Math::AABB3f> meltAABBs; // meltが生成した内接AABB群（meltが無い場合は1個＝全体AABB）
				} occluder;
			};

			std::vector<SubMesh> subMeshes = {};

			std::optional<Skeleton> skeleton; // スケルトンがある場合
		private:
			path_view path; // キャッシュ用のパスビュー

			friend class ModelAssetManager;
		};

		class ModelAssetManager : public
			ResourceManagerBase<ModelAssetManager, ModelAssetHandle, ModelAssetCreateDesc, ModelAssetData> {
		public:
			ModelAssetManager(
				MeshManager& meshMgr,
				MaterialManager& matMgr,
				ShaderManager& shaderMgr,
				PSOManager& psoMgr,
				TextureManager& texMgr,
				BufferManager& cbManager,
				SamplerManager& samplerManager,
				ID3D11Device* device);

			std::optional<ModelAssetHandle> FindExisting(const ModelAssetCreateDesc& d) {
				auto p = std::filesystem::weakly_canonical(d.path);
				if (auto it = pathToHandle.find(p); it != pathToHandle.end()) return it->second;
				return std::nullopt;
			}
			void RegisterKey(const ModelAssetCreateDesc& d, ModelAssetHandle h) {
				pathToHandle.emplace(std::filesystem::weakly_canonical(d.path), h);
			}

			ModelAssetData CreateResource(const ModelAssetCreateDesc& desc, ModelAssetHandle h) {
				return LoadFromGLTF(desc);
			}

			void RemoveFromCaches(uint32_t idx);
			void DestroyResource(uint32_t idx, uint64_t currentFrame);

			// キャッシュ対応：内部で保持して返す
			ModelAssetData LoadFromGLTF(const ModelAssetCreateDesc& desc);

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
				const std::vector<Math::Vec3f>& basePositions,
				const std::vector<Math::Vec3f>* baseNormals,
				const std::vector<Math::Vec4f>* baseTangents,
				const std::vector<Math::Vec2f>* baseUV0,
				const std::vector<std::array<uint8_t, 4>>* baseSkinIdx,
				const std::vector<std::array<uint8_t, 4>>* baseSkinWgt,
				const LodRecipe& recipe,
				MeshManager& meshMgr,
				const std::wstring& tagForCaching,    // 例: path + L"#subX-lodY"
				ModelAssetData::SubmeshLOD& outMesh,
				std::vector<uint32_t>& outIdx,
				MeshManager::RemappedStreams& outStreams,
				bool buildClusters = false,
				bool hasNormal = true,
				bool hasUV = true);

			// 返すのは LOD1..N 用のレシピ（LOD0は常に原型）
			static std::vector<LodRecipe> BuildLodRecipes(const AssetStats& a);

		private:
			MeshManager& meshMgr;
			MaterialManager& matMgr;
			ShaderManager& shaderMgr;
			PSOManager& psoMgr;
			TextureManager& texMgr;
			BufferManager& cbManager;
			SamplerManager& samplerManager;
			ID3D11Device* device;

			// キャッシュ：正規化パス→ModelAsset
			std::unordered_map<std::filesystem::path, ModelAssetHandle> pathToHandle;
		};
	}
}
