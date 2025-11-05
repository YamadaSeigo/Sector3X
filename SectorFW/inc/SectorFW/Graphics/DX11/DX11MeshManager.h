/*****************************************************************//**
 * @file   DX11MeshManager.h
 * @brief DirectX 11のメッシュマネージャーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "DX11MaterialManager.h"
#include "DX11TextureManager.h"

#include <string>
#include <optional>
#include <bitset>
#include <vector>
#include <unordered_map>

 // MeshOptimizerを使う場合は定義
#define USE_MESHOPTIMIZER

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief メッシュ作成のための構造体
		 */
		struct MeshCreateDesc {
			const void* vertices = nullptr;
			uint32_t vSize = {};
			uint32_t stride = {};
			const uint32_t* indices = nullptr;
			uint32_t iSize = {};
			D3D11_USAGE vUsage = D3D11_USAGE_IMMUTABLE;
			D3D11_USAGE iUsage = D3D11_USAGE_IMMUTABLE;
			D3D11_CPU_ACCESS_FLAG cpuAccessFlags = D3D11_CPU_ACCESS_WRITE; // D3D11_USAGE_STAGING用
			std::wstring sourcePath;
		};

		/**
		 * @brief DirectX 11のメッシュデータを定義する構造体
		 */
		struct MeshData {
			// 複数ストリーム（slotごと）のVB
			std::array<ComPtr<ID3D11Buffer>, 8> vbs{}; // 0..7 くらいまで確保
			std::array<UINT, 8> strides{};
			std::array<UINT, 8> offsets{};
			std::bitset<8> usedSlots{};
			uint32_t stride = 0; // 旧：単一VBの互換維持（未使用でも保持）

			// セマンティク（"POSITION0","NORMAL0","TANGENT0","TEXCOORD0","BLENDINDICES0","BLENDWEIGHT0"...）
			struct AttribBinding {
				UINT slot;
				DXGI_FORMAT format;
				UINT alignedByteOffset; // slot内オフセット（今回 NORMAL/TANGENT を同一slotで詰めるなら使用）
			};
			std::unordered_map<std::string, AttribBinding> attribMap;

			ComPtr<ID3D11Buffer> ib = nullptr;
			uint32_t indexCount = 0;
			uint32_t startIndex = 0;

		private:
			std::wstring path; // キャッシュ用パス

			friend class MeshManager;
		};
		/**
		 * @brief DirectX 11のメッシュマネージャークラス
		 */
		class MeshManager : public ResourceManagerBase<MeshManager, MeshHandle, MeshCreateDesc, MeshData> {
		public:
			// ====== LOD 生成や再インデックスで使う公開ユーティリティ ======
						// 頂点ストリームを一括でリマップした結果を保持する簡易構造体
			struct RemappedStreams {
				std::vector<Math::Vec3f> positions;
				std::vector<Math::Vec3f> normals;
				std::vector<Math::Vec4f> tangents;   // w = handedness
				std::vector<Math::Vec2f> tex0;
				std::vector<std::array<uint8_t, 4>> skinIdx;
				std::vector<std::array<uint8_t, 4>> skinWgt;

			};

			/**
			 * @brief コンストラクタ
			 * @param dev DirectX 11のデバイス
			 */
			explicit MeshManager(ID3D11Device* dev) noexcept : device(dev) {
				bool ok = InitCommonMeshes();
				assert(ok && "Failed to initialize common meshes");
			}
			/**
			 * @brief 既存のメッシュを検索する関数
			 * @param d メッシュ作成のための構造体
			 * @return std::optional<MeshHandle> 既存のメッシュハンドル、存在しない場合はstd::nullopt
			 */
			std::optional<MeshHandle> FindExisting(const MeshCreateDesc& d) noexcept {
				if (!d.sourcePath.empty()) {
					if (auto it = pathToHandle.find(d.sourcePath); it != pathToHandle.end())
						return it->second;
				}
				return std::nullopt;
			}
			/**
			 * @brief メッシュハンドルをキャッシュに登録する関数
			 * @param d メッシュ作成のための構造体
			 * @param h メッシュハンドル
			 */
			void RegisterKey(const MeshCreateDesc& d, MeshHandle h) {
				if (!d.sourcePath.empty()) pathToHandle.emplace(d.sourcePath, h);
			}
			/**
			 * @brief メッシュリソースを作成する関数
			 * @param desc メッシュ作成のための構造体
			 * @param h メッシュハンドル
			 * @return DX11MeshData 作成されたメッシュデータ
			 */
			MeshData CreateResource(const MeshCreateDesc& desc, MeshHandle h);
			/**
			 * @brief cgltf から抽出した float 配列を SNORM / half にパックして SoA ストリームを作る
			 * @param pathW メッシュの元ファイルパス（キャッシュ用）
			 * @param positions   : float3（そのまま）
			 * @param normals     : float3 → R8G8B8A8_SNORM（w=0）
			 * @param tangents    : float4 (xyz, w=handedness) → R8G8B8A8_SNORM
			 * @param tex0        : float2 → R16G16_FLOAT（half2）
			 * @param skinIdx     : u8x4 → R8G8B8A8_UINT
			 * @param skinWgt     : u8x4(UNORM) → R8G8B8A8_UNORM (0..255, 正規化済み)
			 */
			bool CreateFromGLTF_SoA_R8Snorm(
				const std::wstring& pathW,
				const std::vector<Math::Vec3f>& positions,
				const std::vector<Math::Vec3f>& normals,         // optional（空なら未使用）
				const std::vector<Math::Vec4f>& tangents,        // optional（空なら未使用）
				const std::vector<Math::Vec2f>& tex0,            // optional
				const std::vector<std::array<uint8_t, 4>>& skinIdx,     // optional
				const std::vector<std::array<uint8_t, 4>>& skinWgt,     // optional (0..255, 後述)
				const std::vector<uint32_t>& indices,
				MeshData& out);
			/**
			 * @brief CreateFromGLTF_SoA_R8Snorm用のラッパー関数
			 * @param pathW メッシュの元ファイルパス（キャッシュ用）
			 * @param positions   : float3（そのまま）
			 * @param normals     : float3 → R8G8B8A8_SNORM（w=0）
			 * @param tangents    : float4 (xyz, w=handedness) → R8G8B8A8_SNORM
			 * @param tex0        : float2 → R16G16_FLOAT（half2）
			 * @param skinIdx     : u8x4 → R8G8B8A8_UINT
			 * @param skinWgt     : u8x4(UNORM) → R8G8B8A8_UNORM (0..255, 正規化済み)
			 * @param indices     : uint32_t インデックス配列
			 * @param outHandle   : 出力メッシュハンドル
			 * @return bool 成功したら true、失敗したら false
			 */
			bool AddFromSoA_R8Snorm(
				const std::wstring& sourcePath,
				const std::vector<Math::Vec3f>& positions,
				const std::vector<Math::Vec3f>& normals,
				const std::vector<Math::Vec4f>& tangents,
				const std::vector<Math::Vec2f>& tex0,
				const std::vector<std::array<uint8_t, 4>>& skinIdx,
				const std::vector<std::array<uint8_t, 4>>& skinWgt,
				const std::vector<uint32_t>& indices,
				MeshHandle& outHandle);

			// meshoptimizer の remap を各ストリームへ適用（存在するストリームのみ処理）
			static void ApplyRemapToStreams(
				const std::vector<uint32_t>& remap,
				const std::vector<Math::Vec3f>& inPos,
				const std::vector<Math::Vec3f>* inNor,
				const std::vector<Math::Vec4f>* inTan,
				const std::vector<Math::Vec2f>* inUV,
				const std::vector<std::array<uint8_t, 4>>* inSkinIdx,
				const std::vector<std::array<uint8_t, 4>>* inSkinWgt,
				size_t outVertexCount,
				RemappedStreams& out);

#ifdef USE_MESHOPTIMIZER
			// クラスタ（meshlet）のメタ情報
			struct ClusterInfo {
				uint32_t triOffset;     // outClusterTriangles 内の三角形先頭（3*triCount 個の連続配列）
				uint32_t triCount;      // 三角形数
				Math::Vec3f center;     // バウンディング球中心
				float       radius;     // バウンディング球半径
				Math::Vec3f coneAxis;   // 法線円錐の軸
				float       coneCutoff; // 円錐カットオフ cosθ

			};

			// positions / indices から meshlets を生成して返す
			// outClusterTriangles: meshletごとの三角形(インデックス)が順に詰まった配列（3*triCount 個）
			// outClusterVertices : meshletごとのユニーク頂点リスト（将来の再インデックス等に使用可）
			static bool BuildClustersWithMeshoptimizer(
				const std::vector<Math::Vec3f>& positions,
				const std::vector<uint32_t>& indices,
				std::vector<ClusterInfo>& outClusters,
				std::vector<uint8_t>& outClusterTriangles,
				std::vector<uint32_t>& outClusterVertices,
				uint32_t maxVertsPerCluster = 64,
				uint32_t maxTrisPerCluster = 124,
				float coneWeight = 0.0f);
#endif

			/**
			 * @brief キャッシュからメッシュハンドルを削除する関数
			 * @param idx 削除するメッシュのインデックス
			 */
			void RemoveFromCaches(uint32_t idx);
			/**
			 * @brief メッシュリソースを破棄する関数
			 * @param idx 破棄するメッシュのインデックス
			 * @param currentFrame 現在のフレーム番号
			 */
			void DestroyResource(uint32_t idx, uint64_t currentFrame);
			/**
			 * @brief メッシュのインデックス数を設定する関数
			 * @param h メッシュハンドル
			 * @param count インデックス数
			 */
			void SetIndexCount(MeshHandle h, uint32_t count, uint32_t start = 0) {
				if (!IsValid(h)) {
					assert(false && "Invalid MeshHandle in SetIndexCount");
					return;
				}
				std::unique_lock lock(mapMutex);
				slots[h.index].data.indexCount = count;
				slots[h.index].data.startIndex = start;
			}

			// スプライト（単位クアッド：中心原点、幅=高さ=1、Z=0）
			MeshHandle GetSpriteQuadHandle() const { return spriteQuadHandle_; }
		private:
			// 共通メッシュを初期化（複数回呼んでも安全）
			bool InitCommonMeshes();

		private:
			ID3D11Device* device;

			std::unordered_map<std::wstring, MeshHandle> pathToHandle;

			// 共通スプライト用ハンドル（無効のままなら未初期化）
			MeshHandle spriteQuadHandle_{};
		};
	}
}
