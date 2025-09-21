/*****************************************************************//**
 * @file   PhysicsContactListener.h
 * @brief 物理エンジンの接触イベントリスナーの実装
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include "PhysicsSnapshot.h"
#include "PhysicsDevice_Util.h"
#include <Jolt/Physics/Collision/ContactListener.h>

namespace SectorFW
{
	namespace Physics
	{
		class PhysicsDevice; // 前方

		/**
		 * @brief 接触イベントの種類を表す名前空間
		 */
		class ContactListenerImpl final : public JPH::ContactListener {
		public:
			explicit ContactListenerImpl(PhysicsDevice* dev) : m_dev(dev) {}
			virtual JPH::ValidateResult OnContactValidate(const JPH::Body& /*body1*/, const JPH::Body& /*body2*/, JPH::RVec3Arg /*baseOffset*/, const JPH::CollideShapeResult& /*result*/) override {
				return JPH::ValidateResult::AcceptAllContactsForThisBodyPair; // マスク制御を入れるならここを拡張
			}
			virtual void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/) override {
				Push(body1, body2, manifold, ContactEvent::Begin);
			}
			virtual void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/) override {
				Push(body1, body2, manifold, ContactEvent::Persist);
			}
			virtual void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
				// manifold無し。代表点は空でOK
				PushRemoved(pair);
			}
		private:
			void Push(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, ContactEvent::Type type);
			void PushRemoved(const JPH::SubShapeIDPair& pair);
			PhysicsDevice* m_dev;
		};
	}
}
