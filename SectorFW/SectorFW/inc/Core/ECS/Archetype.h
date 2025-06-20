#pragma once

#include <vector>
#include <memory>

#include "ArchetypeChunk.h"

namespace SectorFW
{
	namespace ECS
	{
		//----------------------------------------------
		// Archetype (mask-based chunk retrieval)
		//----------------------------------------------
		class Archetype {
		public:
			explicit Archetype(ComponentMask mask) : mask(mask) {}

			ArchetypeChunk* GetOrCreateChunk() {
				for (auto& chunk : chunks) {
					if (chunk->GetEntityCount() < chunk->GetCapacity())
						return chunk.get();
				}
				std::unique_ptr<ArchetypeChunk> newChunk = std::make_unique<ArchetypeChunk>();
				newChunk->InitializeLayoutFromMask(mask);
				ArchetypeChunk* ptr = newChunk.get();
				chunks.emplace_back(std::move(newChunk));

				return ptr;
			}

			const ComponentMask& GetMask() const noexcept { return mask; }
			const std::vector<std::unique_ptr<ArchetypeChunk>>& GetChunks() const noexcept {
				return chunks;
			}

		private:
			ComponentMask mask;
			std::vector<std::unique_ptr<ArchetypeChunk>> chunks;
		};
	}
}
