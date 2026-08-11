/*****************************************************************//**
 * \file   ObjectOrientedStore.h
 * \brief  オブジェクト指向のEntityを管理する
 * \author lenov
 * \date   July 2026
 *********************************************************************/

#pragma once

#include <vector>
#include <unordered_map>

#include "Object.h"
#include "ECS/EntityManager.h"

namespace SFW
{
	class ObjectManager
	{
	public:
		ObjectManager& operator=(const ObjectManager&) = delete;

		void AddObject(std::unique_ptr<Object> obj) {
			Object* rawPtr = obj.get();
			m_entityToObjectMap[obj->GetEntityID()] = std::move(obj);
			m_rootObjects.push_back(rawPtr);
		}

		void CreateObject(std::string name) {
			ECS::EntityID entityID = ECS::EntityManager::EntityIDAllocatorAccessor::Create();
			CreateObject(entityID, name);
		}

		void CreateObject(ECS::EntityID entityID, std::string name) {
			auto obj = std::make_unique<Object>(entityID, name);
			AddObject(std::move(obj));
		}

		template<typename... BehaviourTypes>
			requires (is_behaviour_v<BehaviourTypes> && ...)
		void CreateObjectWithBehaviours(ECS::EntityID entityID, std::string name) {
			auto obj = std::make_unique<Object>(entityID, name);
			(obj->AddBehaviour(std::make_unique<BehaviourTypes>()), ...);
			AddObject(std::move(obj));
		}

		void SetParent(Object* child, Object* parent) {
			if (child && parent) {
				parent->AddChild(child);
				RemoveRootObject(child);
			}
		}

		void SetParent(ECS::EntityID childID, ECS::EntityID parentID) {
			auto childIt = m_entityToObjectMap.find(childID);
			auto parentIt = m_entityToObjectMap.find(parentID);
			if (childIt != m_entityToObjectMap.end() && parentIt != m_entityToObjectMap.end()) {
				SetParent(childIt->second.get(), parentIt->second.get());
			}
		}

		void UpdateAllObjects(const ECS::ServiceLocator& serviceLocator) {
			for (auto& obj : m_rootObjects) {
				obj->Update(serviceLocator);
			}
		}

	private:
		void RemoveRootObject(Object* obj) {
			auto it = std::find(m_rootObjects.begin(), m_rootObjects.end(), obj);
			if (it != m_rootObjects.end()) {
				m_rootObjects.erase(it);
			}
		}

	private:
		std::vector<Object*> m_rootObjects = {};
		std::unordered_map<ECS::EntityID, std::unique_ptr<Object>> m_entityToObjectMap = {};
	};
}
