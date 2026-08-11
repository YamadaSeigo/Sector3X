/*****************************************************************//**
 * \file   Object.h
 * \brief オブジェクト指向のEntity単位
 * \author lenov
 * \date   July 2026
 *********************************************************************/

#pragma once

#include <vector>

#include "ECS/entity.h"
#include "Behaviour.h"
#include "../Math/Transform.hpp"
#include "../Util/Name.h"

namespace SFW
{
	class Object
	{
	public:

		Object(Object&) = delete;
		Object(Object&& other) : m_entityID(other.m_entityID), m_name(std::move(other.m_name)), m_localTransform(std::move(other.m_localTransform)), m_children(std::move(other.m_children)), m_behaviours(std::move(other.m_behaviours)) {
			other.m_entityID = ECS::EntityID::Invalid();
		}

		Object(ECS::EntityID entityID, std::string name) : m_entityID(entityID), m_name(name) {}

		Object& operator=(const Object&) = delete;
		Object& operator=(Object&& other) {
			if (this != &other) {
				m_entityID = other.m_entityID;
				m_name = std::move(other.m_name);
				m_localTransform = std::move(other.m_localTransform);
				m_children = std::move(other.m_children);
				m_behaviours = std::move(other.m_behaviours);
			}
		}

		void AddChild(Object* child) {
			m_children.push_back(child);
		}

		void AddBehaviour(std::unique_ptr<Behaviour> behaviour) {
			m_behaviours.push_back(std::move(behaviour));
		}

		ECS::EntityID GetEntityID() const noexcept {
			return m_entityID;
		}

		void Update(const ECS::ServiceLocator& serviceLocator)
		{
			for (auto& behaviour : m_behaviours) {
				behaviour->Update(serviceLocator);
			}

			CalculateWorldTransform();

			for (auto& child : m_children) {
				child->Update(serviceLocator);
			}
		}

		void CalculateWorldTransform() {
			// 変更がなければ再計算しない（超重要・後述）
			if (!m_isDirty) return;

			// 自身のローカルTransformを行列に変換
			Math::Matrix4x4f localMatrix = m_localTransform.ToMatrix();

			if (m_parent != nullptr) {
				// 親がいる場合：親のワールド行列 × 自身のローカル行列
				m_worldMatrix = localMatrix * m_parent->GetWorldMatrix();
			}
			else {
				// ルートオブジェクト（親がいない）場合：ローカルがそのままワールドになる
				m_worldMatrix = localMatrix;
			}

			// 計算が終わったのでフラグを倒す
			m_isDirty = false;
		}

		// ユーザーが位置を変更するためのセッター
		void SetLocalPosition(const Math::Vec3f& pos) {
			m_localTransform.location = pos;
			SetDirty(); // 動いたので再計算フラグを立てる
		}

		// 自身と、すべての子供のフラグを立てる（伝播）
		void SetDirty() {
			if (m_isDirty) return; // すでに立っていれば何もしない

			m_isDirty = true;
			for (Object* child : m_children) {
				child->SetDirty(); // 親が動けば子も動くので、再帰的にフラグを立てる
			}
		}

		const Math::Matrix4x4f& GetWorldMatrix() const noexcept {
			return m_worldMatrix;
		}

	private:
		ECS::EntityID m_entityID;
		Name m_name;

		Transform m_localTransform;
		Math::Matrix4x4f m_worldMatrix;

		Object* m_parent = nullptr;
		std::vector<Object*> m_children;
		std::vector<std::unique_ptr<Behaviour>> m_behaviours;

		bool m_isDirty = true; // Transformが変更されたかどうかのフラグ
	};
}
