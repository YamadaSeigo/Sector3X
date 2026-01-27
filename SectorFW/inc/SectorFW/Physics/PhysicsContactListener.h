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
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace SFW
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

		// CharacterVirtual 用の ContactListener
		class CharacterContactListenerImpl final : public JPH::CharacterContactListener {
		public:
			explicit CharacterContactListenerImpl(PhysicsDevice* dev) : m_dev(dev) {}

			// ==== Validate フェーズ ====
			// Body と当たる前のフィルタ（全部許可するなら true 固定でOK）
			bool OnContactValidate(const JPH::CharacterVirtual* inCharacter,
				const JPH::BodyID& inBodyID2,
				const JPH::SubShapeID& inSubShapeID2) override
			{
				// 必要なら BodyID からレイヤを見てフィルタ
				return true;
			}

			// Character vs Character 版（今回はとりあえず全部許可）
			bool OnCharacterContactValidate(const JPH::CharacterVirtual* inCharacter,
				const JPH::CharacterVirtual* inOtherCharacter,
				const JPH::SubShapeID& inSubShapeID2) override
			{
				return true;
			}

			// ====  新規接触 ====
			// CharacterVirtual が Body にヒットした瞬間のみ（1回だけ）呼ばれる
			void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
				const JPH::BodyID& inBodyID2,
				const JPH::SubShapeID& inSubShapeID2,
				JPH::RVec3Arg                inContactPosition,
				JPH::Vec3Arg                 inContactNormal,
				JPH::CharacterContactSettings& ioSettings) override
			{
				PushContact(ContactEvent::Begin,
					inCharacter, inBodyID2,
					inContactPosition, inContactNormal);
			}

			// Character vs Character の新規接触
			void OnCharacterContactAdded(const JPH::CharacterVirtual* inCharacter,
				const JPH::CharacterVirtual* inOtherCharacter,
				const JPH::SubShapeID& inSubShapeID2,
				JPH::RVec3Arg                inContactPosition,
				JPH::Vec3Arg                 inContactNormal,
				JPH::CharacterContactSettings& ioSettings) override
			{
				// 必要なら ev.b を Character 側の Entity にして PushContact する
				// 今はスキップしてもOK
			}

			// ==== Solve フェーズ ====
			// 毎ステップ衝突解決に使われる接触。滑り禁止等をやる場合はここで ioNewCharacterVelocity を書き換える
			void OnContactSolve(const JPH::CharacterVirtual* inCharacter,
				const JPH::BodyID& inBodyID2,
				const JPH::SubShapeID& inSubShapeID2,
				JPH::RVec3Arg                inContactPosition,
				JPH::Vec3Arg                 inContactNormal,
				JPH::Vec3Arg                 inContactVelocity,
				const JPH::PhysicsMaterial* inContactMaterial,
				JPH::Vec3Arg                 inCharacterVelocity,
				JPH::Vec3& ioNewCharacterVelocity) override
			{
				// 何もしなければデフォルトの挙動
			}

			void OnCharacterContactSolve(const JPH::CharacterVirtual* inCharacter,
				const JPH::CharacterVirtual* inOtherCharacter,
				const JPH::SubShapeID& inSubShapeID2,
				JPH::RVec3Arg                inContactPosition,
				JPH::Vec3Arg                 inContactNormal,
				JPH::Vec3Arg                 inContactVelocity,
				const JPH::PhysicsMaterial* inContactMaterial,
				JPH::Vec3Arg                 inCharacterVelocity,
				JPH::Vec3& ioNewCharacterVelocity) override
			{
				// ここも同様に必要なければ空でOK
			}

			// Body 側の見かけ速度をいじりたいとき用（ベルトコンベアなど）
			void OnAdjustBodyVelocity(const JPH::CharacterVirtual* inCharacter,
				const JPH::Body& inBody2,
				JPH::Vec3& ioLinearVelocity,
				JPH::Vec3& ioAngularVelocity) override
			{
				// 何もしないなら空でOK
			}

		private:
			void PushContact(ContactEvent::Type type,
				const JPH::CharacterVirtual* ch,
				const JPH::BodyID& bodyID,
				JPH::RVec3Arg pos,
				JPH::Vec3Arg normal);

			PhysicsDevice* m_dev = nullptr;
		};
	}
}
