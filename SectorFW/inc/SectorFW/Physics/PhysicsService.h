// PhysicsService.h
#pragma once
#include "PhysicsTypes.h"
#include "PhysicsSnapshot.h"
#include "PhysicsDevice.h"
#include "PhysicsShapeManager.h"
#include "../Util/SpscRing.hpp"
#include "../Core/ECS/ServiceContext.hpp"
#include "../Core/RegistryTypes.h"

namespace SectorFW
{
	namespace Physics
	{
		class PhysicsService : public ECS::IUpdateService {
		public:
			struct Plan {
				float fixed_dt = 1.0f / 60.0f;
				int   substeps = 1;
				bool  collect_debug = false; // 後でデバッグライン等を拾う用
			};

			// ====== 生成インテント：発生源で「この Entity を作って」と積んでおく ======
			struct CreateIntent {
				Entity     e;
				ShapeHandle h;
				EntityManagerKey owner;
			};

			explicit PhysicsService(PhysicsDevice& device, PhysicsShapeManager& shapeMgr,
				Plan plan = { 1.0f / 60.0f, 1, false }, size_t queueCapacityPow2 = 4096)
				: m_device(device), m_mgr(&shapeMgr), plan(plan), m_queue(queueCapacityPow2) {
				m_device.SetShapeResolver(m_mgr);
			}

			/**
			 * @brief Box 形状を生成する
			 * @param he ボックスサイズの半分の長さ（Vec3f）
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @return ShapeHandle 生成されたボックス形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeBox(Vec3f he, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ BoxDesc{he }, s }, h); return h;
			}
			[[nodiscard]] ShapeHandle MakeSphere(float radius, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ SphereDesc{radius}, s }, h); return h;
			}
			[[nodiscard]] ShapeHandle MakeConvex(const std::vector<Vec3f>& pts, float r = 0.05f, float tol = 0.005f) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ ConvexHullDesc{pts, r, tol}, {{1,1,1}} }, h); return h;
			}
			void ReleaseShape(ShapeHandle h, uint64_t sync = 0) { m_mgr->Release(h, sync); }

			// ====== ゲーム側 API（コマンドを積むだけ）======
			void CreateBody(const CreateBodyCmd& c) { Enqueue(c); }
			void DestroyBody(Entity e) { Enqueue(DestroyBodyCmd{ e }); }
			void Teleport(Entity e, const Mat34f& tm, bool wake = true) { Enqueue(TeleportCmd{ e, wake, tm }); }
			void SetLinearVelocity(Entity e, Vec3f v) { Enqueue(SetLinearVelocityCmd{ e, v }); }
			void SetAngularVelocity(Entity e, Vec3f w) { Enqueue(SetAngularVelocityCmd{ e, w }); }
			void AddImpulse(Entity e, Vec3f p, std::optional<Vec3f> at = std::nullopt) {
				AddImpulseCmd cmd{ e, p, {}, false };
				if (at) { cmd.atWorldPos = *at; cmd.useAtPos = true; }
				Enqueue(cmd);
			}
			void SetKinematicTarget(Entity e, const Mat34f& tm) { Enqueue(SetKinematicTargetCmd{ e, tm }); }
			void SetCollisionMask(Entity e, uint32_t mask) { Enqueue(SetCollisionMaskCmd{ e, mask }); }
			void SetObjectLayer(Entity e, uint16_t layer, uint16_t broad) { Enqueue(SetObjectLayerCmd{ e, layer, broad }); }
			void RayCast(uint32_t reqId, Vec3f o, Vec3f dir, float maxDist) { Enqueue(RayCastCmd{ reqId, o, dir, maxDist }); }

			void Update(double dt) override {
				// 物理は固定時間ステップで進める
				Tick(static_cast<float>(dt));
			}

			// ====== フレーム進行 ======
			// dt: 可変フレーム時間（ゲームループから呼ぶ）
			void Tick(float dt) {
				m_accum += dt;

				while (m_accum + 1e-6f >= plan.fixed_dt) { // 浮動誤差対策の微小マージン
					DrainAllToDevice();                   // ここで一括適用
					m_device.Step(plan.fixed_dt, plan.substeps);

					// スナップショットを組み立てる
					m_device.BuildSnapshot(m_snapshot);
					m_prevSnapshot = std::move(m_currSnapshot); // 前フレームのスナップショットを保存
					m_currSnapshot = std::move(m_snapshot);      // 今回のスナップショットを更新

					m_accum -= plan.fixed_dt;
				}
			}

			void BuildPoseBatch(PoseBatchView& v) const {
				m_device.ReadPosesBatch(v);
			}

			// 補間に使うための α を取得（描画フレームで使う）
			float GetAlpha() const {
				return (plan.fixed_dt > 0.0f) ? (m_accum / plan.fixed_dt) : 0.0f;
			}

			// 現在（最後の fixed step 後）のスナップショット参照を返す
			const PhysicsSnapshot& CurrentSnapshot() const { return m_currSnapshot; }
			const PhysicsSnapshot& PreviousSnapshot() const { return m_prevSnapshot; }

			// ====== 生成済み BodyID の参照（差し込み用）======
			std::optional<JPH::BodyID> TryGetBodyID(Entity e) const noexcept {
				return m_device.TryGetBodyID(e);
			}

			void EnqueueCreateIntent(Entity e, ShapeHandle h, const EntityManagerKey& owner) {
				std::scoped_lock lk(m_intentMutex);
				m_createIntents.push_back({ e, h, owner });
			}
			void ConsumeCreateIntents(std::vector<CreateIntent>& out) {
				std::scoped_lock lk(m_intentMutex);
				out.swap(m_createIntents); // O(1)
			}

			// Body 作成完了イベントの取り出し（WriteBack用）
			struct CreatedBody { Entity e; EntityManagerKey owner; JPH::BodyID id; };
			void ConsumeCreatedBodies(std::vector<CreatedBody>& out) {
				std::vector<PhysicsDevice::CreatedBody> tmp;
				m_device.ConsumeCreatedBodies(tmp);
				out.clear(); out.reserve(tmp.size());
				for (auto& x : tmp) out.push_back(CreatedBody{ x.e, x.owner, x.id });
			}

			const PhysicsShapeManager* GetShapeManager() const noexcept { return m_mgr; }

			std::optional<ShapeDims> GetShapeDims(ShapeHandle h) const {
				auto shape = m_mgr->Resolve(h);
				return m_mgr->GetShapeDims(shape);
			}

		private:
			template<class T>
			void Enqueue(const T& c) {
				PhysicsCommand cmd = c;
				// 失敗（満杯）の場合はリトライ or 一時的にブロッキングに切替など、運用ポリシー次第
				while (!m_queue.push(cmd)) { /* backoff/yield */ }
			}

			void DrainAllToDevice() {
				while (auto cmd = m_queue.pop()) {
					m_device.ApplyCommand(*cmd);
				}
			}

		private:
			PhysicsDevice& m_device;
			PhysicsShapeManager* m_mgr{ nullptr }; // 所有しない
			SpscRing<PhysicsCommand> m_queue;

			// 生成インテント
			mutable std::mutex         m_intentMutex;
			std::vector<CreateIntent>  m_createIntents;

			Plan plan;

			float m_accum = 0.0f;

			PhysicsSnapshot m_snapshot;     // 今回ステップで組み立てた一時
			PhysicsSnapshot m_prevSnapshot; // 前フレーム
			PhysicsSnapshot m_currSnapshot; // 現フレーム
		public:
			STATIC_SERVICE_TAG
		};
	}
}