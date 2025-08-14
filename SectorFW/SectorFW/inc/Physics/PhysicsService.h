// PhysicsService.h
#pragma once
#include "PhysicsTypes.h"
#include "PhysicsSnapshot.h"
#include "PhysicsDevice.h"
#include "PhysicsShapeManager.h"
#include "../Util/SpscRing.hpp"
#include "../Core/ECS/ServiceContext.hpp"

namespace SectorFW
{
	namespace Physics
	{
		class PhysicsService : public ECS::IUpdateService{
		public:
			explicit PhysicsService(PhysicsDevice& device, PhysicsShapeManager& shapeMgr, 
				PhysicsDevice::Plan plan = { 1.0f / 60.0f, 1, false }, size_t queueCapacityPow2 = 4096)
				: m_device(device), m_mgr(&shapeMgr), plan(plan), m_queue(queueCapacityPow2) {
				m_device.SetPlan(plan);
				m_device.SetShapeResolver(m_mgr);
			}

			// 糖衣：形状の作成/参照管理は委譲
			ShapeHandle MakeBox(Vec3f he, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ BoxDesc{he }, s }, h); return h;
			}
			ShapeHandle MakeConvex(const std::vector<Vec3f>& pts, float r = 0.05f, float tol = 0.005f) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ ConvexHullDesc{pts, r, tol}, {{1,1,1}} }, h); return h;
			}
			void ReleaseShape(ShapeHandle h, uint64_t sync = 0) { m_mgr->Release(h, sync); }

			// ====== ゲーム側 API（コマンドを積むだけ）======
			void CreateBody(const CreateBodyCmd& c) { Enqueue(c); }
			void DestroyBody(Entity e) { Enqueue(DestroyBodyCmd{ e }); }
			void Teleport(Entity e, const Mat34f& tm, bool wake = true) { Enqueue(TeleportCmd{ e, tm, wake }); }
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
					m_device.Step();
					m_accum -= plan.fixed_dt;

					// 最新スナップショットを internal に保存（外に参照を渡す方針でもOK）
					m_snapshot.poses.clear();
					m_snapshot.contacts.clear();
					m_snapshot.rayHits.clear();
					m_device.BuildSnapshot(m_snapshot);

					// prev/curr のダブルバッファを運用したい場合はここで入れ替え・保持
					m_prevSnapshot = m_currSnapshot;
					m_currSnapshot = m_snapshot; // コピー（必要ならムーブや参照化）
				}
			}

			// 補間に使うための α を取得（描画フレームで使う）
			float GetAlpha(float fixed_dt) const {
				return (fixed_dt > 0.0f) ? (m_accum / fixed_dt) : 0.0f;
			}

			// 現在（最後の fixed step 後）のスナップショット参照を返す
			const PhysicsSnapshot& CurrentSnapshot() const { return m_currSnapshot; }
			const PhysicsSnapshot& PreviousSnapshot() const { return m_prevSnapshot; }

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

			PhysicsDevice::Plan plan;

			float m_accum = 0.0f;

			PhysicsSnapshot m_snapshot;     // 今回ステップで組み立てた一時
			PhysicsSnapshot m_prevSnapshot; // 前フレーム
			PhysicsSnapshot m_currSnapshot; // 現フレーム
		public:
			STATIC_SERVICE_TAG
		};
	}
}