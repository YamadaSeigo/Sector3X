/*****************************************************************//**
 * @file   RenderTypes.h
 * @brief レンダー関連の基本的な型と定数を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../Math/Matrix.hpp"

namespace SFW
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
		 * @details m[3][3]が0.0fならインスタンスデータを保持していない
		 */
		struct InstanceData
		{
			Math::Matrix4x4f worldMtx;
			Math::Vec4f color = Math::Vec4f(1.0f, 1.0f, 1.0f, 1.0f);

			InstanceData() : worldMtx(Math::Matrix4x4f::Identity()) {
				worldMtx[3][3] = 0.0f; // 無効化
			}
			InstanceData(const Math::Matrix4x4f& mtx) : worldMtx(mtx) {
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
		 * @details DrawCommand は index のみでOK（軽量・32B化しやすい）。
		 * @details 条件：フレーム短命 + Pin / Unpin + in - flight 制御 + 投入時の generation 検証。
		 */
		struct DrawCommand {
			uint64_t sortKey = 0;          // 0..63: ソートキー（PSO/材質/メッシュ/深度バケツ等をパック）

			uint32_t mesh = 0;             // 24Bまで：ハンドル/ID（32bit想定）
			uint32_t material = 0;
			uint32_t pso = 0;
			InstanceIndex instanceIndex = {0};    // InstanceData プールへのインデックス

			// ここが“空白の活用”パート（合計 8B）
			uint32_t cbOffsetDiv256 = 0;   // 動的CBリングの 256B 単位オフセット（DX12/11.1 等で使える）
			uint16_t viewMask = 0;         // 例: 16 ビュー/パス（例: 影カスケード / ステレオ / MRT パスの選別）
			uint8_t  flags = 0;            // 小さなフラグ群（下に定義）
			uint8_t  userTag = 0;          // 任意のラベル/デバッグ/可視化タグ

			DrawCommand() noexcept = default;

			// 便利ヘルパ
			void setCBOffsetBytes(uint32_t byteOffset) noexcept { cbOffsetDiv256 = byteOffset >> 8; }
			uint32_t getCBOffsetBytes() const noexcept { return cbOffsetDiv256 << 8; }
		};
		/**
		 * @brief DrawCommand のフラグビットフィールド
		 */
		enum class DrawFlags : uint8_t {
			RebindPSONeeded = 1u << 0,   // PSO再バインドが必要
			// ... あと7ビット分
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
			ShadowBiasLow,
			ShadowBiasMedium,
			ShadowBiasHigh,
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
			Subtract,     // One / Subtract
			// ...
			MAX_COUNT,    // 有効なブレンドステートの数
		};
		/**
		 * @brief 深度ステンシルステートID列挙型
		 */
		enum class DepthStencilStateID {
			Default,							// DepthTest ON, ZWrite ON
			DepthReadOnly,						// DepthTest ON, ZWrite OFF
			Default_Greater,					// DepthTest(Greater) ON, ZWrite ON
			DepthReadOnly_Greater,				// DepthTest(Greater) ON, ZWrite OFF
			Default_Stencil,					// DepthTest ON, ZWrite ON , Stencil ON
			DepthReadOnly_Stencil,			// DepthTest ON, ZWrite OFF Stencil ON
			DepthReadOnly_Greater_Read_Stencil,		// DepthTest(Greater) ON, ZWrite OFF Stencil ON
			NoDepth,							// DepthTest OFF, ZWrite OFF
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
			float occlusionFactor = 1.0f;
			uint32_t hasFlags = 0; // フラグビットフィールド

			enum HasFlagsBits : uint32_t {
				HasBaseColorTex			= 1u << 0,
				HasNormalTex			= 1u << 1,
				HasMetallicRoughnessTex = 1u << 2,
				HasOcclusionTex			= 1u << 3,
				HasORMCombined			= 1u << 4,
				HasEmissiveTex			= 1u << 5,
				// ...
			};
		};


		/**
		 * @brief シェーダステージ列挙型
		 */
		enum class ShaderStage { VS, PS /* 将来: GS, HS, DS, CS */ };
		/**
		 * @brief シェーダバリアントID型
		 */
		using ShaderVariantID = uint32_t;

		/**
		 * @brief バッファバインドスロット構造体
		 */
		struct BindSlotBuffer {
			uint32_t slot = 0;
			BufferHandle handle = {};

			BindSlotBuffer(BufferHandle h) : handle(h) {}
			BindSlotBuffer(uint32_t slot, BufferHandle h) : slot(slot), handle(h) {}

			BindSlotBuffer& operator=(const BufferHandle& h) {
				handle = h;
				return *this;
			}
		};

		/**
		 * @brief ビューポート構造体
		 */
		struct Viewport {
			float topLeftX = 0.0f;
			float topLeftY = 0.0f;
			float width = 0.0f;
			float height = 0.0f;
			float minDepth = 0.0f;
			float maxDepth = 1.0f;
		};

		/**
		 * @brief デフォルトのビュー・ハンドル管理構造体
		 */
		template<typename T>
		struct DefaultViewHandle {
			using type = std::add_pointer_t<std::remove_pointer_t<T>>;

			DefaultViewHandle() = default;
			DefaultViewHandle(type p) : ptr(p) {}

			DefaultViewHandle& operator=(type p) {
				ptr = p;
				return *this;
			}

			type Get() const { return ptr; }
		private:
			type ptr = nullptr;
		};
	}
}
