// PhysicsDevice.h
#pragma once
#include "PhysicsTypes.h"
#include "PhysicsSnapshot.h"
#include "PhysicsContactListener.h"
#include "IShapeResolver.h"
#include <unordered_map>

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>

namespace SectorFW
{
	namespace Physics
	{
		// ===== 内部バッファ（スナップショット前に溜める） =====
		struct PendingRayHit {
			uint32_t requestId;
			bool     hit{ false };
			Entity   entity{ 0 };
			Vec3f    pos{};
			Vec3f    normal{};
			float    distance{ 0 };
		};

		class PhysicsDevice; // 前方宣言

		class MyContactListenerOwner {
		public:
			ContactListenerImpl listener;
			MyContactListenerOwner(PhysicsDevice* dev) : listener(dev) {}
		};

		// SoA バッファ（あなたの Archetype チャンクが持つビュー）
		struct PoseBatchView {
			// N 個のエンティティ分のポインタ（連続配列）
			float* posX; float* posY; float* posZ;
			float* rotX; float* rotY; float* rotZ; float* rotW;
			uint8_t* updatedMask;      // 書けたら 1（任意）
			size_t  count;

			// 対応する BodyID が SoA と同じ順序で並んでいる前提（超重要）
			const JPH::BodyID* bodyIDs;
			const uint8_t* isStaticMask;  // 静的なら 1 → 読み飛ばす（任意）
		};

		// Kinematic ターゲットを SoA からまとめて適用（物理固定ステップの PreStep で）
		struct KinematicBatchView {
			const JPH::BodyID* bodyIDs; // SoA順
			const float* posX; const float* posY; const float* posZ;
			const float* rotX; const float* rotY; const float* rotZ; const float* rotW;
			const uint8_t* maskKinematic; // 1:キネマのみ適用
			size_t count;
		};

		class PhysicsDevice {
			struct BodyIDHash {
				size_t operator()(const JPH::BodyID& id) const noexcept {
					return std::hash<uint32_t>{}(id.GetIndexAndSequenceNumber());
				}
			};

			struct BodyIDEq {
				bool operator()(const JPH::BodyID& a, const JPH::BodyID& b) const noexcept {
					return a.GetIndexAndSequenceNumber() == b.GetIndexAndSequenceNumber();
				}
			};

		public:
			struct Plan {
				float fixed_dt = 1.0f / 60.0f;
				int   substeps = 1;
				bool  collect_debug = false; // 後でデバッグライン等を拾う用
			};

			struct InitParams {
				uint32_t maxBodies = 100000;
				uint32_t maxBodyPairs = 1024 * 64;
				uint32_t maxContactConstraints = 1024 * 64;
				int      workerThreads = -1; // -1 = auto
				// BroadPhaseLayerInterface, Filters, などは後でセットでも可
			};

			PhysicsDevice() = default;
			~PhysicsDevice() { Shutdown(); }

			bool Initialize(const InitParams& p);
			bool IsInitialized() const noexcept { return m_initialized; }
			void Shutdown();

			void SetPlan(const Plan& plan) noexcept {
				this->m_plan = plan;
			}

			// 1 fixed-step 中に呼ぶ：事前キューを適用
			void ApplyCommand(const PhysicsCommand& cmd);

			// 物理を1ステップ進める
			void Step();

			// スナップショット抽出（poses / contacts / rayHits）
			void BuildSnapshot(PhysicsSnapshot& out);

			void ReadPosesBatch(const PoseBatchView& out_soav);

			void ApplyKinematicTargetsBatch(const KinematicBatchView& v);

			// --- Entity <-> BodyID 紐付け（外部は使わない想定でもOK）
			std::optional<JPH::BodyID> FindBody(Entity e) const;

			// ContactListener から使う小さなアクセサ（物理スレッド内のみ呼ぶ想定）
			void   PushContactEvent(const ContactEvent& ev) { m_pendingContacts.push_back(ev); }
			Entity ResolveEntity(const JPH::BodyID& id) const {
				auto it = m_b2e.find(id);
				return (it != m_b2e.end()) ? it->second : Entity{};
			}

			void SetShapeResolver(const IShapeResolver* r) noexcept { m_shapeResolver = r; }
		private:
			// Jolt本体
			JPH::PhysicsSystem         m_physics;
			JPH::TempAllocatorImpl* m_tempAlloc = nullptr;
			JPH::JobSystemThreadPool* m_jobs = nullptr;

			// 加工しやすい参照
			JPH::BodyInterface* m_bi = nullptr;

			// Entity <-> BodyID
			std::unordered_map<Entity, JPH::BodyID> m_e2b;
			std::unordered_map<JPH::BodyID, Entity, BodyIDHash, BodyIDEq> m_b2e;

			std::unique_ptr<MyContactListenerOwner> m_contactListener;
			std::vector<ContactEvent> m_pendingContacts;
			std::vector<PendingRayHit> m_pendingRayHits;

			Plan m_plan = { 1.0f / 60.0f, 1, false }; // デフォルトプラン

			bool m_initialized = false;

			// ---- ここから下は実装のフック ----
			void ApplyCreate(const CreateBodyCmd& c);
			void ApplyDestroy(const DestroyBodyCmd& c);
			void ApplyTeleport(const TeleportCmd& c);
			void ApplySetLinVel(const SetLinearVelocityCmd& c);
			void ApplySetAngVel(const SetAngularVelocityCmd& c);
			void ApplyAddImpulse(const AddImpulseCmd& c);
			void ApplySetKinematicTarget(const SetKinematicTargetCmd& c);
			void ApplySetCollisionMask(const SetCollisionMaskCmd& c);
			void ApplySetObjectLayer(const SetObjectLayerCmd& c);
			void ApplyRayCast(const RayCastCmd& c);

			const IShapeResolver* m_shapeResolver = nullptr;

			// 形状取得（あなたの ShapeManager から引いて JPH::RefConst<Shape> を返す想定）
			JPH::RefConst<JPH::Shape> ResolveShape(ShapeHandle h) const {
				// テスト容易性のためここ経由にする
				if (!m_shapeResolver) return nullptr;
				return m_shapeResolver->Resolve(h);
			}

			// ContactListener などはメンバに持って、Step 中にイベントをバッファへ
			// （ここでは簡略化：BuildSnapshot で m_physics からポーズだけ吸い上げる例）
		};
	}
}