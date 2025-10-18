/*****************************************************************//**
 * @file   RenderTypes.h
 * @brief レンダー関連の基本的な型と定数を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../Math/Matrix.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief レンダリングキューのバッファ数（トリプルバッファリング推奨）
		 */
		static inline constexpr uint16_t RENDER_BUFFER_COUNT = 3;
		/**
		 * @brief メッシュハンドル構造体
		 */
		struct MeshHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief マテリアルハンドル構造体
		 */
		struct MaterialHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief シェーダハンドル構造体
		 */
		struct ShaderHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief パイプラインステートオブジェクトハンドル構造体
		 */
		struct PSOHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief テクスチャハンドル構造体
		 */
		struct TextureHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief バッファハンドル構造体
		 */
		struct BufferHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief サンプラーハンドル構造体
		 */
		struct SamplerHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief モデルアセットハンドル構造体
		 */
		struct ModelAssetHandle { uint32_t index; uint32_t generation; };
		/**
		 * @brief インスタンスデータ構造体
		 * @detail m[3][3]が0.0fならインスタンスデータを保持していない
		 */
		struct InstanceData
		{
			Math::Matrix4x4f worldMtx;

			InstanceData() : worldMtx(Math::Matrix4x4f::Identity()) {
				worldMtx[3][3] = 0.0f; // 無効化
			}
			InstanceData(const Math::Matrix4x4f& mtx) : worldMtx(mtx) {
				worldMtx[3][3] = 1.0f; // 有効化
			}
			InstanceData(Math::Matrix4x4f&& mtx) noexcept : worldMtx(std::move(mtx)) {
				worldMtx[3][3] = 1.0f; // 有効化
			}

			bool HasData() const noexcept { return worldMtx[3][3] != 0.0f; }
			void SetData(const Math::Matrix4x4f& mtx) noexcept { worldMtx = mtx; worldMtx[3][3] = 1.0f; }
		};
		/**
		 * @brief インスタンスデータのインデックス構造体
		 */
		struct InstanceIndex {
			uint32_t index = 0;
			InstanceIndex& operator=(const uint32_t& idx) { index = idx; return *this; }
		};
		/**
		 * @brief ソートキーを生成するヘルパ関数
		 * @param psoIndex PSOインデックス
		 * @param materialIndex マテリアルインデックス
		 * @param meshIndex メッシュインデックス
		 * @return uint64_t ソートキー
		 */
		inline uint64_t MakeSortKey(uint32_t psoIndex, uint32_t materialIndex, uint32_t meshIndex) {
			return (static_cast<uint64_t>(psoIndex) << 40) |
				(static_cast<uint64_t>(materialIndex) << 20) |
				static_cast<uint64_t>(meshIndex);
		}

		/**
		 * @brief 描画コマンド構造体
		 * @detail DrawCommand は index のみでOK（軽量・32B化しやすい）。
		 * @detail 条件：フレーム短命 + Pin / Unpin + in - flight 制御 + 投入時の generation 検証。
		 */
		struct DrawCommand {
			uint64_t sortKey;          // 0..63: ソートキー（PSO/材質/メッシュ/深度バケツ等をパック）

			uint32_t mesh;             // 24Bまで：ハンドル/ID（32bit想定）
			uint32_t material;
			uint32_t pso;
			InstanceIndex instanceIndex;    // InstanceData プールへのインデックス

			// ここが“空白の活用”パート（合計 8B）
			uint32_t cbOffsetDiv256;   // 動的CBリングの 256B 単位オフセット（DX12/11.1 等で使える）
			uint16_t viewMask;         // 例: 16 ビュー/パス（影カスケード / ステレオ / MRT パスの選別）
			uint8_t  flags;            // 小さなフラグ群（下に定義）
			uint8_t  userTag;          // 任意のラベル/デバッグ/可視化タグ

			DrawCommand() : sortKey(0), mesh(0), material(0), pso(0), instanceIndex{ 0 },
				cbOffsetDiv256(0), viewMask(0xFFFF), flags(0), userTag(0) {
			}

			// 便利ヘルパ
			void setCBOffsetBytes(uint32_t byteOffset) noexcept { cbOffsetDiv256 = byteOffset >> 8; }
			uint32_t getCBOffsetBytes() const noexcept { return cbOffsetDiv256 << 8; }
		};
		/**
		 * @brief DrawCommand のフラグビットフィールド
		 */
		enum DrawFlags : uint8_t {
			DF_BindPSONeeded = 1u << 0, // 前のコマンドから PSO を切り替える必要あり
			DF_BindMaterial = 1u << 1, // マテリアルバインドが必要
			DF_BindMesh = 1u << 2, // VB/IB 再バインドが必要
			DF_AlphaTest = 1u << 3, // αテスト有無（ソートキーにも入れて良い）
			DF_ShadowCaster = 1u << 4, // 影を落とす
			DF_DoubleSided = 1u << 5, // 両面
			DF_Skinned = 1u << 6, // スキンあり（シェーダバリアント切替のヒント）
			// 1bit 余り
		};
		/**
		 * @brief マテリアルテンプレートID列挙型
		 */
		enum class MaterialTemplateID : uint32_t {
			PBR = 0,
			Unlit,
			Toon,
			// ...
			MAX_COUNT, // 有効なテンプレートの数
		};
		/**
		 * @brief プリミティブトポロジ列挙型
		 */
		enum class PrimitiveTopology {
			Undefined,
			PointList,
			LineList,
			LineStrip,
			TriangleList,
			TriangleStrip,
			LineListAdj,
			LineStripAdj,
			TriangleListAdj,
			TriangleStripAdj,
			Patch1,
			Patch2,
			// ... Patch3～Patch32 など必要に応じて
			MAX_COUNT, // ここまでが有効なトポロジ
		};
		/**
		 * @brief ラスタライザーステートID列挙型
		 */
		enum class RasterizerStateID {
			SolidCullBack,
			SolidCullFront,
			SolidCullNone,
			WireCullBack,
			WireCullFront,
			WireCullNone,
			// ...
			MAX_COUNT, // 有効なラスタライザーステートの数
		};
		/**
		 * @brief ブレンドステートID列挙型
		 */
		enum class BlendStateID {
			Opaque,       // No blending
			AlphaBlend,   // SrcAlpha / InvSrcAlpha
			Additive,     // SrcAlpha / One
			Multiply,     // DestColor / Zero
			// ...
			MAX_COUNT,    // 有効なブレンドステートの数
		};
		/**
		 * @brief 深度ステンシルステートID列挙型
		 */
		enum class DepthStencilStateID {
			Default,          // DepthTest ON, ZWrite ON
			DepthReadOnly,    // DepthTest ON, ZWrite OFF
			NoDepth,          // DepthTest OFF, ZWrite OFF
			// ...
			MAX_COUNT,        // 有効な深度ステンシルステートの数
		};
		/**
		 * @brief PBRマテリアル用定数バッファ構造体
		 */
		struct alignas(16) PBRMaterialCB {
			float baseColorFactor[4] = { 1,1,1,1 };
			float metallicFactor = 1.0f;
			float roughnessFactor = 1.0f;
			float hasBaseColorTex = 0.0f;
			float hasNormalTex = 0.0f;
			float hasMRRTex = 0.0f;
			float _pad[3] = { 0,0,0 }; // 16B境界
		};
		/**
		 * @brief シェーダステージ列挙型
		 */
		enum class ShaderStage { VS, PS /* 将来: GS, HS, DS, CS */ };
		/**
		 * @brief シェーダバリアントID型
		 */
		using ShaderVariantID = uint32_t;
	}
}
