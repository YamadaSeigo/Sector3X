#pragma once

#include "Math/Matrix.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		static inline constexpr uint16_t RENDER_QUEUE_BUFFER_COUNT = 2;

		struct MeshHandle { uint32_t index; uint32_t generation; };
		struct MaterialHandle { uint32_t index; uint32_t generation; };
		struct ShaderHandle { uint32_t index; uint32_t generation; };
		struct PSOHandle { uint32_t index; uint32_t generation; };
		struct TextureHandle { uint32_t index; uint32_t generation; };
		struct BufferHandle { uint32_t index; uint32_t generation; };
		struct SamplerHandle { uint32_t index; uint32_t generation; };
		struct ModelAssetHandle { uint32_t index; uint32_t generation; };

		struct InstanceData
		{
			Math::Matrix4x4f worldMtx;
		};

		inline uint64_t MakeSortKey(uint32_t psoIndex, uint32_t materialIndex, uint32_t meshIndex) {
			return (static_cast<uint64_t>(psoIndex) << 40) |
				(static_cast<uint64_t>(materialIndex) << 20) |
				static_cast<uint64_t>(meshIndex);
		}

		struct DrawCommand {
			uint64_t sortKey = {};
			MeshHandle mesh = {};
			MaterialHandle material = {};
			PSOHandle pso = {};
			InstanceData instance = {};

			DrawCommand() = default;
			explicit DrawCommand(
				MeshHandle meshHandle,
				MaterialHandle materialHandle,
				PSOHandle psoHandle,
				InstanceData instanceData) noexcept :
				mesh(meshHandle), material(materialHandle), pso(psoHandle), instance(instanceData) {
				sortKey = MakeSortKey(pso.index, material.index, mesh.index);
			}
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
			SolidCullNone,
			Wireframe,
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
