#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <vector>

#include "../Core/ECS/ServiceContext.hpp"
#include "RenderQueue.h"
#include "../Util/TypeChecker.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		struct RenderService
		{
			template<typename Backend, PointerType RTV, PointerType SRV, PointerType Buffer>
			friend class RenderGraph;

			RenderService() : queueMutex(std::make_unique<std::shared_mutex>()) {}

			RenderQueue::ProducerSession GetProducerSession(const std::string& passName)
			{
				std::shared_lock lock(*queueMutex);

				auto it = queueIndex.find(passName);
				if (it == queueIndex.end()) {
					assert(false && "RenderQueue not found for pass name");
				}

				return renderQueues[it->second]->MakeProducer();
			}
			RenderQueue::ProducerSession GetProducerSession(size_t index)
			{
				std::shared_lock lock(*queueMutex);
				if (index >= renderQueues.size()) {
					assert(false && "RenderQueue index out of range");
				}
				return renderQueues[index]->MakeProducer();
			}

			template<typename ResourceType>
			ResourceType* GetResourceManager()
			{
				auto it = resourceManagers.find(typeid(ResourceType));
				if (it == resourceManagers.end()) {
					assert(false && "Resource manager not found for type");
					return nullptr;
				}
				return static_cast<ResourceType*>(it->second);
			}
		private:
			template<typename ResourceType>
			void RegisterResourceManager(ResourceType* manager)
			{
				if (!manager) {
					assert(false && "Cannot register null resource manager");
					return;
				}
				if (resourceManagers.contains(typeid(ResourceType))) {
					assert(false && "Resource manager already registered for this type");
					return;
				}

				resourceManagers[typeid(ResourceType)] = manager;
			}
		private:
			std::unordered_map<std::string, size_t> queueIndex;
			std::vector<std::unique_ptr<RenderQueue>> renderQueues; // 全てのレンダークエリを保持する
			std::unique_ptr<std::shared_mutex> queueMutex;
			std::unordered_map<std::type_index, void*> resourceManagers;
			uint64_t currentFrame = 0; // 現在のフレーム番号
		public:
			STATIC_SERVICE_TAG
		};
	}
}
