#include <bit>

#include "Core/ECS/ComponentLayoutRegistry.h"
#include "Core/ECS/ComponentTypeRegistry.h"
#include "Core/ECS/ArchetypeChunk.h"

namespace SFW
{
	namespace ECS
	{
		const ComponentLayout& ComponentLayoutRegistry::GetLayout(const ComponentMask& mask) noexcept
		{
			std::lock_guard<std::mutex> lock(map_mutex);

			if (componentLayout.contains(mask)) {
				return componentLayout.at(mask);
			}
			else {
				AddNewComponentLayout(mask);
				return componentLayout.at(mask);
			}
		}

		void ComponentLayoutRegistry::AddNewComponentLayout(const ComponentMask& mask)
		{
			ComponentLayout layout;

			struct Entry {
				ComponentTypeID id;
				const ComponentMeta& meta;
			};

			std::vector<Entry> component;

			bool hasSoA = false;

			// 最小ループでビット走査
			ComponentMask working = mask;
			while (working.any()) {
				ComponentTypeID index = static_cast<ComponentTypeID>(std::countr_zero(working.to_ullong()));
				working.reset(index);
				const ComponentMeta* meta = ComponentTypeRegistry::GetMeta(index);
				if (meta == nullptr) break;
				if (meta->isSparse) continue;

				if (meta->isSoA) {
					hasSoA = true;
				}

				component.emplace_back(index, *meta);
			}

			if (hasSoA)
			{
				const auto getTotalSize = [](const std::vector<Entry>& layout, size_t count)->size_t {
					size_t offset = 0;

					for (const auto& entry : layout) {
						for (const auto& structure : entry.meta.structure) {
							offset = AlignTo(offset, structure.align);
							offset += structure.size * count;
						}
					}

					return offset;
					};

				size_t low = 0, high = ChunkSizeBytes;

				while (low < high) {
					size_t mid = (low + high + 1) / 2;
					if (getTotalSize(component, mid) <= ChunkSizeBytes) {
						low = mid;
					}
					else {
						high = mid - 1;
					}
				}

				layout.capacity = low;

				size_t offset = 0;
				for (auto& c : component) {
					OneOrMore<ComponentInfo> infoData;
					infoData.reserve(c.meta.structure.size());
					for (auto& structure : c.meta.structure)
					{
						offset = AlignTo(offset, structure.align);
						infoData.add({ offset, structure.size });
						offset += structure.size * low;
					}
					layout.info.push_back(std::move(infoData));
					layout.infoIdx[c.id] = static_cast<uint32_t>(layout.info.size() - 1);
				}
			}
			else //!hasSoA
			{
				size_t totalSize = 0;
				for (auto& c : component) {
					for (const auto& structure : c.meta.structure) {
						totalSize = AlignTo(totalSize, structure.align);
						totalSize += structure.size;
					}
				}

				layout.capacity = totalSize > 0 ? ChunkSizeBytes / totalSize : 0;

				while (true) {
					size_t offset = 0;
					bool fits = true;
					for (auto& c : component) {
						// SoAはここでは処理しない
						assert(c.meta.isSoA == false);

						for (const auto& structure : c.meta.structure) {
							if (structure.size == 0) continue; // サイズが0の構造体はスキップ
							offset = AlignTo(offset, structure.align);
							if (offset + structure.size * layout.capacity > ChunkSizeBytes) {
								fits = false;
								break;
							}
							OneOrMore<ComponentInfo> infoData;
							infoData.add({ offset, structure.size });
							layout.info.push_back(std::move(infoData));
							layout.infoIdx[c.id] = static_cast<uint32_t>(layout.info.size() - 1);
							offset += structure.size * layout.capacity;
						}
					}
					if (fits || layout.capacity == 0)break;

					--layout.capacity;
					layout.info.clear();
					layout.infoIdx.clear();
				}
			}// !hasSoA

			componentLayout[mask] = std::move(layout);
		}
	}
}