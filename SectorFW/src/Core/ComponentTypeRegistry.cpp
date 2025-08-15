#include "Core/ECS/ComponentTypeRegistry.h"
#include "message.h"

namespace SectorFW
{
	namespace ECS
	{
		const ComponentMeta& ComponentTypeRegistry::GetMeta(ComponentTypeID id) noexcept
		{
			auto iter = meta.find(id);

			DYNAMIC_ASSERT_MESSAGE(iter != meta.end(), "Not Registry ComponentType!");

			return iter->second;
		}
	}
}