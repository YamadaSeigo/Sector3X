// PhysicsDevice.cpp
#include "Physics/PhysicsDevice.h"
#include "Physics/PhysicsDevice_Util.h"
#include "Physics/PhysicsLayers.h"

// Jolt
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>

namespace SFW::Physics {
	// ===== PhysicsDevice メンバ追加（ヘッダに追記を想定） =====
	// BroadPhase & Filters & Listener は所有
	static BroadPhaseLayerInterfaceImpl           g_bpInterface;
	static ObjectVsBroadPhaseLayerFilterImpl      g_ovsbFilter;
	static ObjectLayerPairFilterImpl              g_pairFilter;

	// ===== Initialize / Shutdown =====
	bool PhysicsDevice::Initialize(const InitParams& p) {
		assert(!m_initialized && "PhysicsDevice is already initialized!");
		m_initialized = true;

		// Jolt グローバル
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		// Allocator / JobSystem
		const int hw = (int)std::thread::hardware_concurrency();
		const int workers = (p.workerThreads <= 0) ? (std::max)(1, hw - 1) : p.workerThreads;

		m_tempAlloc = new JPH::TempAllocatorImpl(16 * 1024 * 1024); // 16MB（調整可）
		m_jobs = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

		// PhysicsSystem 構築
		m_physics.Init(
			p.maxBodies,
			0, // numBodyMutexes（0=自動）
			p.maxBodyPairs,
			p.maxContactConstraints,
			g_bpInterface,
			g_ovsbFilter,
			g_pairFilter
		);

		// 各種設定（必要に応じて調整）
		auto settings = m_physics.GetPhysicsSettings();
		settings.mBaumgarte = 0.2f;
		settings.mNumPositionSteps = 1;
		settings.mNumVelocitySteps = 1;
		settings.mDeterministicSimulation = true;
		m_physics.SetPhysicsSettings(settings);

		// BodyInterface
		m_bi = &m_physics.GetBodyInterface();

		// ContactListener
		m_contactListener.reset(new MyContactListenerOwner(this));
		m_physics.SetContactListener(&m_contactListener->listener);

#ifdef ENABLE_CHARACTER_CONTACT_LISTENER
		m_characterContactListener = std::make_unique<CharacterContactListenerImpl>(this);
#endif

		return true;
	}

	void PhysicsDevice::Shutdown() {
		if (m_bi) {
			// 可能なら全ボディ削除（デバッグ時）
		}

		m_physics.SetContactListener(nullptr);
		m_contactListener.reset();

		delete m_jobs;       m_jobs = nullptr;
		delete m_tempAlloc;  m_tempAlloc = nullptr;

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	void PhysicsDevice::ApplyCommand(const PhysicsCommand& cmd) {
		std::visit([this](auto&& c) {
			using C = std::decay_t<decltype(c)>;
			if constexpr (std::is_same_v<C, CreateBodyCmd>)          ApplyCreate(c);
			else if constexpr (std::is_same_v<C, DestroyBodyCmd>)    ApplyDestroy(c);
			else if constexpr (std::is_same_v<C, TeleportCmd>)       ApplyTeleport(c);
			else if constexpr (std::is_same_v<C, SetLinearVelocityCmd>)  ApplySetLinVel(c);
			else if constexpr (std::is_same_v<C, SetAngularVelocityCmd>) ApplySetAngVel(c);
			else if constexpr (std::is_same_v<C, AddImpulseCmd>)     ApplyAddImpulse(c);
			else if constexpr (std::is_same_v<C, SetKinematicTargetCmd>) ApplySetKinematicTarget(c);
			else if constexpr (std::is_same_v<C, SetCollisionMaskCmd>)   ApplySetCollisionMask(c);
			else if constexpr (std::is_same_v<C, SetObjectLayerCmd>)     ApplySetObjectLayer(c);
			else if constexpr (std::is_same_v<C, RayCastCmd>)            ApplyRayCast(c);
			else if constexpr (std::is_same_v<C, CreateCharacterCmd>)       ApplyCreateCharacter(c);
			else if constexpr (std::is_same_v<C, SetCharacterVelocityCmd>)  ApplySetCharacterVelocity(c);
			else if constexpr (std::is_same_v<C, SetCharacterRotationCmd>)  ApplySetCharacterRotation(c);
			else if constexpr (std::is_same_v<C, TeleportCharacterCmd>)     ApplyTeleportCharacter(c);
			else if constexpr (std::is_same_v<C, DestroyCharacterCmd>)      ApplyDestroyCharacter(c);
			}, cmd);
	}

	// ---- Create ----
	void PhysicsDevice::ApplyCreate(const CreateBodyCmd& c) {
		// 形状解決(ShapeManager から取得）
		JPH::RefConst<JPH::Shape> shape = ResolveShape(c.shape);
		if (!shape) return;

		// MotionType 推定：layerに応じて静的/動的/センサー
		JPH::EMotionType motion = JPH::EMotionType::Dynamic;
		if (c.kinematic)                      motion = JPH::EMotionType::Kinematic;
		else if (c.layer == Layers::NON_MOVING) motion = JPH::EMotionType::Static;

		JPH::BodyCreationSettings bc(shape, ToJVec3(c.worldTM.pos), ToJQuat(c.worldTM.rot), motion, c.layer);
		bc.mFriction = c.friction;
		bc.mRestitution = c.restitution;
		if (motion == JPH::EMotionType::Dynamic) {
			bc.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia; // 密度指定も可
			bc.mMassPropertiesOverride.mMass = c.density * 0.001f; // 便宜上（実際は体積から計算を推奨）
		}

		JPH::Body* body = m_bi->CreateBody(bc); // 生成のみ
		if (!body) return;

		// センサーならセンサー扱い（無反発などのフラグ調整は Contact で）
		if (c.layer == Layers::SENSOR) {
			body->SetIsSensor(true);
		}

		auto id = body->GetID();
		m_bi->AddBody(id, JPH::EActivation::Activate);

		// 対応表登録
		m_e2b[c.e] = id;
		m_b2e.emplace(id, c.e);

		// 作成完了イベントを貯める（後段で BodyComponent に差し込み）
		{
			std::scoped_lock lk(m_createdMutex);
			m_created.push_back(CreatedBody{ c.e, c.owner, id });
		}
	}

	// ---- Destroy ----
	void PhysicsDevice::ApplyDestroy(const DestroyBodyCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		JPH::BodyID id = it->second;

		m_bi->RemoveBody(id);
		m_bi->DestroyBody(id);
		m_b2e.erase(id);
		m_e2b.erase(it);
	}

	// ---- Teleport ----
	void PhysicsDevice::ApplyTeleport(const TeleportCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		m_bi->SetPositionAndRotation(it->second, ToJVec3(c.worldTM.pos), ToJQuat(c.worldTM.rot),
			c.wake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	// ---- Velocities ----
	void PhysicsDevice::ApplySetLinVel(const SetLinearVelocityCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		m_bi->SetLinearVelocity(it->second, ToJVec3(c.v));
	}
	void PhysicsDevice::ApplySetAngVel(const SetAngularVelocityCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		m_bi->SetAngularVelocity(it->second, ToJVec3(c.w));
	}

	// ---- Impulse ----
	void PhysicsDevice::ApplyAddImpulse(const AddImpulseCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		if (c.useAtPos)
			m_bi->AddImpulse(it->second, ToJVec3(c.impulse), ToJVec3(c.atWorldPos));
		else
			m_bi->AddImpulse(it->second, ToJVec3(c.impulse));
	}

	// ---- Kinematic Target ----
	void PhysicsDevice::ApplySetKinematicTarget(const SetKinematicTargetCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		//m_bi->SetKinematicTarget(it->second, ToJMatRT(c.worldTM));
	}

	// ---- Collision Mask（※最小骨組み：詳細実装はプロジェクト方針に依存） ----
	void PhysicsDevice::ApplySetCollisionMask(const SetCollisionMaskCmd& /*c*/) {
		// 方針：Joltでは ObjectLayer/GroupFilter/ContactFilter の組み合わせで実現。
		// ここでは骨組みのみ（プロジェクトの要求に合わせて GroupFilterTable 等で実装）。
	}

	// ---- ObjectLayer 変更 ----
	void PhysicsDevice::ApplySetObjectLayer(const SetObjectLayerCmd& c) {
		auto it = m_e2b.find(c.e);
		if (it == m_e2b.end()) return;
		// 直接 Layer を差し替え（Jolt には BodyInterface::SetObjectLayer がある）
		m_bi->SetObjectLayer(it->second, c.layer);
	}

	// ---- RayCast ----
	void PhysicsDevice::ApplyRayCast(const RayCastCmd& c) {
		JPH::RRayCast rc;
		rc.mOrigin = JPH::Vec3(c.origin.x, c.origin.y, c.origin.z);
		rc.mDirection = JPH::Vec3(c.dir.x * c.maxDist, c.dir.y * c.maxDist, c.dir.z * c.maxDist);

		JPH::RayCastResult hit{}; // ioHit（初期値は「最遠」扱い）

		// 単一ヒット版のオーバーロード：RayCastSettings は取りません
		const bool any = m_physics.GetNarrowPhaseQuery().CastRay(
			rc,
			hit,
			BroadPhaseLayerFilterMask{c.broadPhaseMask},
			ObjectLayerFilterMask{c.objectLayerMask},
			RayBodyFilterIgnoreSelf{ (JPH::BodyID)c.ignoreBody }
		);

		PendingRayHit r{};
		r.requestId = c.requestId;
		r.hit = any;

		if (any) {
			// ヒット点
#if defined(JPH_DOUBLE_PRECISION) && JPH_DOUBLE_PRECISION
			JPH::RVec3 hitPos = rc.GetPointOnRay(hit.mFraction);
#else
			JPH::Vec3  hitPos = rc.GetPointOnRay(hit.mFraction);
#endif
			r.pos = FromJVec3(hitPos);
			r.distance = hit.mFraction * c.maxDist;

			// 法線を正しく取得（ドキュメントの指示通り Body 経由）
			{
				JPH::BodyLockRead lock(m_physics.GetBodyLockInterface(), hit.mBodyID);
				if (lock.Succeeded()) {
					const JPH::Body& body = lock.GetBody();
					auto n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPos);
					r.normal = FromJVec3(n);
				}
			}

			// BodyID -> Entity
			auto it = m_b2e.find(hit.mBodyID);
			r.entity = (it != m_b2e.end()) ? it->second : Entity(0);
		}

		m_pendingRayHits.emplace_back(r);
	}

	void PhysicsDevice::ApplyCreateCharacter(const CreateCharacterCmd& c)
	{
		// ShapeManager から JPH::Shape を解決
		JPH::RefConst<JPH::Shape> shape = ResolveShape(c.shape);
		if (!shape) return;

		JPH::CharacterVirtualSettings settings;
		settings.mShape = shape;
		settings.mUp = JPH::Vec3::sAxisY();
		settings.mMaxSlopeAngle = JPH::DegreesToRadians(c.maxSlopeDeg);
		// 必要に応じて SupportingVolume や MaxStrength など設定

		JPH::Vec3 pos = ToJVec3(c.worldTM.pos);
		JPH::Quat rot = ToJQuat(c.worldTM.rot);

		// CharacterVirtual は PhysicsSystem を使って広義の「ワールド」と衝突検出する
		auto* system = &m_physics;

		JPH::Ref<JPH::CharacterVirtual> ch =
			new JPH::CharacterVirtual(&settings, pos, rot, system);

#ifdef ENABLE_CHARACTER_CONTACT_LISTENER
		ch->SetListener(m_characterContactListener.get());
#endif

		m_characters[c.e] = { ch, c.objectLayer };

		// CharacterVirtual* -> Entity の逆引き
		m_charToEntity[ch.GetPtr()] = c.e;
	}

	void PhysicsDevice::ApplySetCharacterVelocity(const SetCharacterVelocityCmd& c)
	{
		auto it = m_characters.find(c.e);
		if (it == m_characters.end()) return;
		it->second.ref->SetLinearVelocity(ToJVec3(c.v));
	}

	void PhysicsDevice::ApplySetCharacterRotation(const SetCharacterRotationCmd& c)
	{
		auto it = m_characters.find(c.e);
		if (it == m_characters.end()) return;
		it->second.ref->SetRotation(ToJQuat(c.rot));
	}

	void PhysicsDevice::ApplyTeleportCharacter(const TeleportCharacterCmd& c)
	{
		auto it = m_characters.find(c.e);
		if (it == m_characters.end()) return;
		it->second.ref->SetPosition(ToJVec3(c.worldTM.pos));
		it->second.ref->SetRotation(ToJQuat(c.worldTM.rot));
	}

	void PhysicsDevice::ApplyDestroyCharacter(const DestroyCharacterCmd& c)
	{
		auto it = m_characters.find(c.e);
		if (it == m_characters.end()) return;

		// JPH::Ref<JPH::CharacterVirtual> なので erase すれば参照カウントが減って破棄される
		m_characters.erase(it);
	}


	// ===== Step =====
	void PhysicsDevice::Step(float fixed_dt, int substeps) {
		m_physics.Update(
			fixed_dt,      // 合計Δt（固定）
			substeps,      // サブステップ数（衝突/統合の内部分割）
			m_tempAlloc,
			m_jobs    // ここで内部が並列化
		);

		// 2. CharacterVirtual のステップ
		if (!m_characters.empty())
		{
			// Gravity は PhysicsSystem から取る
			JPH::Vec3 gravity = m_physics.GetGravity();

			JPH::BodyFilter                   bodyFilter;
			JPH::ShapeFilter                  shapeFilter;

			for (auto& [e, ch] : m_characters)
			{
				// フィルタはプロジェクトのレイヤ設計に合わせて
				JPH::DefaultBroadPhaseLayerFilter bpFilter(g_ovsbFilter, ch.layer/*キャラ用BroadPhaseLayer*/);
				JPH::DefaultObjectLayerFilter     objFilter(g_pairFilter, ch.layer/*キャラが衝突する ObjectLayer 組み合わせ*/);

				ch.ref->Update(
					fixed_dt,
					gravity,
					bpFilter,
					objFilter,
					bodyFilter,
					shapeFilter,
					*m_tempAlloc
				);
			}
		}
	}

	// ===== Snapshot =====
	void PhysicsDevice::BuildSnapshot(PhysicsSnapshot& out) {

		{
			// --- Contacts（ContactListener が溜めたものを吐き出す） ---
			std::scoped_lock lk(m_pendingContactsMutex);
			out.contacts.insert(out.contacts.end(), m_pendingContacts.begin(), m_pendingContacts.end());
			m_pendingContacts.clear();
		}

		// --- RayHits ---
		out.rayHits.reserve(out.rayHits.size() + m_pendingRayHits.size());
		for (auto& r : m_pendingRayHits) {
			out.rayHits.push_back(RayCastHitEvent{
				r.requestId, r.hit, r.entity, r.pos, r.normal, r.distance
				});
		}
		m_pendingRayHits.clear();
	}

	void PhysicsDevice::ReadPosesBatch(const PoseBatchView& v)
	{
		using namespace JPH;
		constexpr size_t kChunk = 128; // まとめロック単位（環境で調整）

		size_t i = 0;
		while (i < v.count) {
			const size_t n = (std::min)(kChunk, v.count - i);

			// BodyID のチャンク先頭アドレス
			const BodyID* ids = v.bodyIDs + i;

			// 複数ボディを**一括で Read ロック**
			BodyLockMultiRead lock(m_physics.GetBodyLockInterface(), ids, (int)n);
			for (size_t j = 0; j < n; ++j) {
				const size_t idx = i + j;
				const BodyID id = ids[j];

				if (v.isStaticMask && v.isStaticMask[idx]) {
					if (v.updatedMask) v.updatedMask[idx] = 0;
					continue; // 静的は通常スキップ
				}

				// Pending（未生成）やロック失敗（破棄中など）はスキップ
				if (IsPendingBodyID(id)) {
					if (v.updatedMask) v.updatedMask[idx] = 0;
					continue;
				}

				const Body* b = lock.GetBody((int)j);

				if (b == nullptr) [[unlikely]] {
					LOG_ERROR("PhysicsDevice::ReadPosesBatch: BodyLockMultiRead failed");
					if (v.updatedMask) v.updatedMask[idx] = 0;
					continue;
				}
				if (b->IsStatic()) { if (v.updatedMask) v.updatedMask[idx] = 0; continue; }

#if defined(JPH_DOUBLE_PRECISION) && JPH_DOUBLE_PRECISION
				const JPH::RVec3 p = b.GetPosition();
#else
				const JPH::Vec3  p = b->GetPosition();
#endif
				const JPH::Quat  q = b->GetRotation();

				v.posX[idx] = (float)p.GetX();
				v.posY[idx] = (float)p.GetY();
				v.posZ[idx] = (float)p.GetZ();

				v.rotX[idx] = q.GetX();
				v.rotY[idx] = q.GetY();
				v.rotZ[idx] = q.GetZ();
				v.rotW[idx] = q.GetW();

				if (v.updatedMask) v.updatedMask[idx] = 1;
			}

			i += n;
		}
	}

	void PhysicsDevice::ApplyKinematicTargetsBatch(const KinematicBatchView& v, float fixed_dt)
	{
		using namespace JPH;
		constexpr size_t kChunk = 128; // ロック粒度(環境に合わせて 64-256 でチューニング)

		size_t i = 0;
		while (i < v.count) {
			const size_t n = (std::min)(kChunk, v.count - i);
			const BodyID* ids = v.bodyIDs + i;

			BodyLockMultiWrite lock(m_physics.GetBodyLockInterface(), ids, (int)n);
			for (size_t j = 0; j < n; ++j) {
				const size_t idx = i + j;
				const BodyID id = ids[j];
				if (v.maskKinematic && !v.maskKinematic[idx]) continue;
				//if (!lock.Succeeded(j)) continue;

				// Pending（未生成）やロック失敗（破棄中など）はスキップ
				if (IsPendingBodyID(id)) continue;

				Body* b = lock.GetBody((int)j);
				if (b->GetMotionType() != EMotionType::Kinematic) continue;

#if defined(JPH_DOUBLE_PRECISION) && JPH_DOUBLE_PRECISION
				RVec3 targetPos(v.posX[idx], v.posY[idx], v.posZ[idx]);
#else
				Vec3  targetPos(v.posX[idx], v.posY[idx], v.posZ[idx]);
#endif
				Quat  targetRot(v.rotX[idx], v.rotY[idx], v.rotZ[idx], v.rotW[idx]);

				// ---- 推奨パス：ロック済み Body に直接 MoveKinematic ----
				// ※ Jolt の Body にはロック下で呼べる MoveKinematic が用意されています
				b->MoveKinematic(targetPos, targetRot, fixed_dt);
			}

			i += n;
		}
	}

	// ===== FindBody =====
	std::optional<JPH::BodyID> PhysicsDevice::FindBody(Entity e) const {
		auto it = m_e2b.find(e);
		if (it == m_e2b.end()) return std::nullopt;
		return it->second;
	}

	std::optional<CharacterPose> PhysicsDevice::GetCharacterPose(Entity e)
	{
		auto it = m_characters.find(e);
		if (it != m_characters.end())
		{
			auto chara = it->second.ref;
			CharacterPose pose(chara);
			return pose;
		}

		return std::nullopt;
	}

	// ===== ContactListenerImpl =====

	void ContactListenerImpl::Push(const JPH::Body& a, const JPH::Body& b,
		const JPH::ContactManifold& m,
		ContactEvent::Type type)
	{
		ContactEvent ev{};
		ev.type = type;
		ev.a = m_dev->ResolveEntity(a.GetID());
		ev.b = m_dev->ResolveEntity(b.GetID());

		if (!m.mRelativeContactPointsOn1.empty()) {
			// 代表点と法線（Joltのコメント準拠）
			auto hitPos = a.GetWorldTransform() * m.mRelativeContactPointsOn1[0];
			ev.pointWorld = FromJVec3(hitPos);
			ev.normalWorld = FromJVec3(m.mWorldSpaceNormal);
		}
		ev.impulse = 0.0f; // 必要なら別の収集ポイントで

		m_dev->PushContactEvent(ev);
	}

	void ContactListenerImpl::PushRemoved(const JPH::SubShapeIDPair& pair)
	{
		ContactEvent ev{};
		ev.type = ContactEvent::End;
		ev.a = m_dev->ResolveEntity(pair.GetBody1ID());
		ev.b = m_dev->ResolveEntity(pair.GetBody2ID());
		m_dev->PushContactEvent(ev);
	}

	// ===== CharacterContactListenerImpl =====
	void CharacterContactListenerImpl::PushContact(ContactEvent::Type            type,
		const JPH::CharacterVirtual* ch,
		const JPH::BodyID& bodyID,
		JPH::RVec3Arg                pos,
		JPH::Vec3Arg                 normal)
	{
		ContactEvent ev{};
		ev.type = type;

		// a: キャラクターの Entity
		ev.a = m_dev->ResolveCharacterEntity(ch);

		// b: 当たった Body の Entity（既存の ResolveEntity を流用）
		ev.b = m_dev->ResolveEntity(bodyID);

		ev.pointWorld = FromJVec3(pos);     // すでに RVec3/Vec3 → 自前 Vec3 の変換関数がありますよね
		ev.normalWorld = FromJVec3(normal);
		ev.impulse = 0.0f;               // CharacterContactListener からはインパルスは取れないので 0 など

		m_dev->PushContactEvent(ev);
	}

	/*Entity ResolveEntityFromBodyID(PhysicsDevice* dev, const JPH::BodyID& id) {
		auto it = dev->m_b2e.find(id);
		return (it != dev->m_b2e.end()) ? it->second : 0;
	}*/
}