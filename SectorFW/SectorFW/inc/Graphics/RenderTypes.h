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
		struct ConstantBufferHandle { uint32_t index; uint32_t generation; };
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
		};
	}
}
