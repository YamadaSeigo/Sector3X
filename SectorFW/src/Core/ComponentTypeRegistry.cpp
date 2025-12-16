#include "Core/ECS/ComponentTypeRegistry.h"
#include "message.h"

namespace SFW
{
	namespace ECS
	{
		const ComponentMeta* ComponentTypeRegistry::GetMeta(ComponentTypeID id) noexcept
		{
			auto iter = meta.find(id);

			if (iter == meta.end())
			{
				DYNAMIC_ASSERT_MESSAGE(false, "Not Registry ComponentType!");
				return nullptr;
			}

			return &iter->second;
		}
	}
}