#pragma once

#include "../Math/Matrix.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		static inline constexpr uint16_t RENDER_QUEUE_BUFFER_COUNT = 3;

		struct MeshHandle { uint32_t index; uint32_t generation; };
		struct MaterialHandle { uint32_t index; uint32_t generation; };
		struct ShaderHandle { uint32_t index; uint32_t generation; };
		struct PSOHandle { uint32_t index; uint32_t generation; };
		struct TextureHandle { uint32_t index; uint32_t generation; };
		struct BufferHandle { uint32_t index; uint32_t generation; };
		struct SamplerHandle { uint32_t index; uint32_t generation; };
		struct ModelAssetHandle { uint32_t index; uint32_t generation; };

		struct alignas(16) InstanceData
		{
			Math::Matrix4x4f worldMtx;
		};

		struct InstanceIndex { uint32_t index = 0; };

		inline uint64_t MakeSortKey(uint32_t psoIndex, uint32_t materialIndex, uint32_t meshIndex) {
			return (static_cast<uint64_t>(psoIndex) << 40) |
				(static_cast<uint64_t>(materialIndex) << 20) |
				static_cast<uint64_t>(meshIndex);
		}

		//DrawCommand は index のみでOK（軽量・32B化しやすい）。
		//条件：フレーム短命 + Pin / Unpin + in - flight 制御 + 投入時の generation 検証。
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

		enum class MaterialTemplateID : uint32_t {
			PBR = 0,
			Unlit,
			Toon,
			// ...
			MAX_COUNT, // 有効なテンプレートの数
		};

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

		enum class BlendStateID {
			Opaque,       // No blending
			AlphaBlend,   // SrcAlpha / InvSrcAlpha
			Additive,     // SrcAlpha / One
			Multiply,     // DestColor / Zero
			// ...
			MAX_COUNT,    // 有効なブレンドステートの数
		};

		enum class DepthStencilStateID {
			Default,          // DepthTest ON, ZWrite ON
			DepthReadOnly,    // DepthTest ON, ZWrite OFF
			NoDepth,          // DepthTest OFF, ZWrite OFF
			// ...
			MAX_COUNT,        // 有効な深度ステンシルステートの数
		};

		struct alignas(16) PBRMaterialCB {
			float baseColorFactor[4] = { 1,1,1,1 };
			float metallicFactor = 1.0f;
			float roughnessFactor = 1.0f;
			float hasBaseColorTex = 0.0f;
			float hasNormalTex = 0.0f;
			float hasMRRTex = 0.0f;
			float _pad[3] = { 0,0,0 }; // 16B境界
		};

		enum class ShaderStage { VS, PS /* 将来: GS, HS, DS, CS */ };

		using ShaderVariantID = uint32_t;
	}
}
