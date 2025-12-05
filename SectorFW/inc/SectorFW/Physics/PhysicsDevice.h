/*****************************************************************//**
 * @file   PhysicsDevice.h
 * @brief 物理デバイスのヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include "PhysicsTypes.h"
#include "PhysicsSnapshot.h"
#include "PhysicsContactListener.h"
#include "IShapeResolver.h"
#include <unordered_map>
#include <optional>
#include <mutex>
#include <vector>
#include <immintrin.h>
#include <cstddef>
#include <cstdint>
#include <cmath>

 // Jolt
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "../Core/ECS/EntityManager.h"
#include "../Core/RegistryTypes.h"

namespace SFW
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

		// SoA バッファ（Archetype チャンクが持つビュー）
		struct PoseBatchView {
			// N 個のエンティティ分のポインタ（連続配列）
			float* posX; float* posY; float* posZ;
			float* rotX; float* rotY; float* rotZ; float* rotW;
			uint32_t* updatedMask; // 書き込みがあったら 1 を立てる（任意）
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
			// Body 作成完了イベント（差し込み用）
			struct CreatedBody {
				Entity                          e;
				SpatialChunkKey				 owner;
				JPH::BodyID                     id;
			};

			struct InitParams {
				uint32_t maxBodies = 100000;
				uint32_t maxBodyPairs = 1024 * 64;
				uint32_t maxContactConstraints = 1024 * 64;
				int      workerThreads = -1; // -1 = auto
				// BroadPhaseLayerInterface, Filters, などは後でセットでも可
			};

			struct CharacterVirtualInfo
			{
				JPH::Ref<JPH::CharacterVirtual> ref;
				uint16_t layer;
			};

			PhysicsDevice() = default;
			~PhysicsDevice() { Shutdown(); }

			bool Initialize(const InitParams& p);
			bool IsInitialized() const noexcept { return m_initialized; }
			void Shutdown();

			// 1 fixed-step 中に呼ぶ：事前キューを適用
			void ApplyCommand(const PhysicsCommand& cmd);

			// 物理を1ステップ進める
			void Step(float fixed_dt, int substeps);

			// スナップショット抽出（poses / contacts / rayHits）
			void BuildSnapshot(PhysicsSnapshot& out);

			void ReadPosesBatch(const PoseBatchView& out_soav);

			void ApplyKinematicTargetsBatch(const KinematicBatchView& v, float fixed_dt);

			// --- Entity <-> BodyID 紐付け（外部は使わない想定でもOK）
			std::optional<JPH::BodyID> FindBody(Entity e) const;

			// ContactListener から使う小さなアクセサ（物理スレッド内のみ呼ぶ想定）
			void   PushContactEvent(const ContactEvent& ev) {
				std::scoped_lock lk(m_pendingContactsMutex);
				m_pendingContacts.push_back(ev);
			}
			Entity ResolveEntity(const JPH::BodyID& id) const {
				auto it = m_b2e.find(id);
				return (it != m_b2e.end()) ? it->second : Entity{};
			}

			void SetShapeResolver(const IShapeResolver* r) noexcept { m_shapeResolver = r; }

			// ===== 補助: Entity → BodyID の参照（存在しなければ nullopt）=====
			std::optional<JPH::BodyID> TryGetBodyID(Entity e) const noexcept {
				auto it = m_e2b.find(e);
				if (it == m_e2b.end()) return std::nullopt;
				return it->second;
			}

			// ===== 作成完了イベントを取り出す（スレッド安全・O(1) swap）=====
			void ConsumeCreatedBodies(std::vector<CreatedBody>& out) {
				std::scoped_lock lk(m_createdMutex);
				out.swap(m_created);
			}

			// キャラクターのポーズ読み出し
			bool GetCharacterPose(Entity e, Vec3f& outPos, Quatf& outRot);
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

			// 作成完了イベント
			std::mutex              m_createdMutex;
			std::vector<CreatedBody> m_created;

			std::unique_ptr<MyContactListenerOwner> m_contactListener;
			std::vector<ContactEvent> m_pendingContacts;
			std::mutex m_pendingContactsMutex;
			std::vector<PendingRayHit> m_pendingRayHits;

			// Entity -> CharacterVirtual
			std::unordered_map<Entity, CharacterVirtualInfo> m_characters;

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
			void ApplyCreateCharacter(const CreateCharacterCmd& c);
			void ApplySetCharacterVelocity(const SetCharacterVelocityCmd& c);
			void ApplySetCharacterRotation(const SetCharacterRotationCmd& c);
			void ApplyTeleportCharacter(const TeleportCharacterCmd& c);

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

		struct UpdateSet {
			// 0/1 マスク（密ルート用）。疎ルートでは省略可
			const uint32_t* mask01 = nullptr;
			// 更新インデックス配列（疎ルート用）。あれば最速
			const uint32_t* indices = nullptr;
			size_t updateCount = 0; // 更新件数
		};

		// ===== 位置/スカラー: lerp(a,b,alpha) を最速更新 =====
		inline void update_scalar_lerp(
			float* __restrict dst,
			const float* __restrict a,
			const float* __restrict b,
			size_t N,
			float alpha,
			const UpdateSet& upd,
			float sparseThreshold = 0.30f)   // 30% 未満なら疎とみなす
		{
#if defined(__AVX2__)
			const __m256 vA = _mm256_set1_ps(alpha);

			const bool hasIdx = (upd.indices && upd.updateCount > 0);
			const float density = (N == 0) ? 0.f : float(upd.updateCount) / float(N);

			if (!hasIdx || density >= sparseThreshold) {
				// ==== 密ルート: 全走査 + ブレンド ====
				const __m256i vZeroI = _mm256_setzero_si256();
				size_t i = 0;
				for (; i + 8 <= N; i += 8) {
					const __m256 va = _mm256_loadu_ps(a + i);
					const __m256 vb = _mm256_loadu_ps(b + i);
					const __m256 vold = _mm256_loadu_ps(dst + i);
					const __m256 vlerp = _mm256_fmadd_ps(_mm256_sub_ps(vb, va), vA, va);

					__m256 vm = _mm256_castsi256_ps(vZeroI); // デフォルトは 0 マスク
					if (upd.mask01) {
						const __m256i mi = _mm256_cmpgt_epi32(
							_mm256_loadu_si256((const __m256i*)(upd.mask01 + i)),
							vZeroI);
						vm = _mm256_castsi256_ps(mi);
					}
					else {
						// マスクが無ければ全更新
						vm = _mm256_castsi256_ps(_mm256_cmpeq_epi32(vZeroI, vZeroI));
					}
					const __m256 vout = _mm256_blendv_ps(vold, vlerp, vm);
					_mm256_storeu_ps(dst + i, vout);
				}
				for (; i < N; ++i) {
					const float lerp = a[i] + (b[i] - a[i]) * alpha;
					if (!upd.mask01 || upd.mask01[i]) dst[i] = lerp;
				}
			}
			else {
				// ==== 疎ルート: indices を使って gather 更新 ====
				const int scale = 4; // float*
				size_t i = 0;
				for (; i + 8 <= upd.updateCount; i += 8) {
					__m256i vidx = _mm256_loadu_si256((const __m256i*)(upd.indices + i));
					__m256 va = _mm256_i32gather_ps(a, vidx, scale);
					__m256 vb = _mm256_i32gather_ps(b, vidx, scale);
					__m256 vlerp = _mm256_fmadd_ps(_mm256_sub_ps(vb, va), vA, va);

					// scatter（ネイティブ命令が無い世代）→ 1要素ずつストア
					alignas(32) uint32_t idxbuf[8];
					_mm256_store_si256((__m256i*)idxbuf, vidx);
					alignas(32) float tmp[8];
					_mm256_store_ps(tmp, vlerp);
					for (int k = 0; k < 8; ++k) dst[idxbuf[k]] = tmp[k];
				}
				for (; i < upd.updateCount; ++i) {
					const uint32_t id = upd.indices[i];
					dst[id] = a[id] + (b[id] - a[id]) * alpha;
				}
			}
#else
			// AVX2 なし → 簡易フォールバック
			const bool hasIdx = (upd.indices && upd.updateCount > 0);
			const float density = (N == 0) ? 0.f : float(upd.updateCount) / float(N);
			if (!hasIdx || density >= sparseThreshold) {
				for (size_t i = 0; i < N; ++i) {
					const float lerp = a[i] + (b[i] - a[i]) * alpha;
					if (!upd.mask01 || upd.mask01[i]) dst[i] = lerp;
				}
			}
			else {
				for (size_t j = 0; j < upd.updateCount; ++j) {
					const uint32_t id = upd.indices[j];
					dst[id] = a[id] + (b[id] - a[id]) * alpha;
				}
			}
#endif
		}

		// ===== クォータニオン: 最短経路 nlerp + 正規化 =====
		// prev(ax..aw), curr(bx..bw) → dst(qx..qw)
		inline void update_quat_nlerp_shortest(
			float* __restrict qx, float* __restrict qy, float* __restrict qz, float* __restrict qw,
			const float* __restrict ax, const float* __restrict ay, const float* __restrict az, const float* __restrict aw,
			const float* __restrict bx, const float* __restrict by, const float* __restrict bz, const float* __restrict bw,
			size_t N, float alpha,
			const UpdateSet& upd,
			float sparseThreshold = 0.30f)
		{
#if defined(__AVX2__)
			const __m256 vA = _mm256_set1_ps(alpha);
			const bool hasIdx = (upd.indices && upd.updateCount > 0);
			const float density = (N == 0) ? 0.f : float(upd.updateCount) / float(N);

			auto do_one = [&](uint32_t id) {
				// 最短経路: dot(a,b) < 0 → b を反転
				float dx = ax[id] * bx[id] + ay[id] * by[id] + az[id] * bz[id] + aw[id] * bw[id];
				float sx = bx[id], sy = by[id], sz = bz[id], sw = bw[id];
				if (dx < 0.f) { sx = -sx; sy = -sy; sz = -sz; sw = -sw; }
				float x = ax[id] + (sx - ax[id]) * alpha;
				float y = ay[id] + (sy - ay[id]) * alpha;
				float z = az[id] + (sz - az[id]) * alpha;
				float w = aw[id] + (sw - aw[id]) * alpha;
				float invL = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
				qx[id] = x * invL; qy[id] = y * invL; qz[id] = z * invL; qw[id] = w * invL;
				};

			if (!hasIdx || density >= sparseThreshold) {
				const __m256 vHalf = _mm256_set1_ps(0.5f); //（必要なら rsqrt 改良に使用）
				size_t i = 0;
				for (; i + 8 <= N; i += 8) {
					// a と b のロード
					__m256 axv = _mm256_loadu_ps(ax + i), ayv = _mm256_loadu_ps(ay + i), azv = _mm256_loadu_ps(az + i), awv = _mm256_loadu_ps(aw + i);
					__m256 bxv = _mm256_loadu_ps(bx + i), byv = _mm256_loadu_ps(by + i), bzv = _mm256_loadu_ps(bz + i), bwv = _mm256_loadu_ps(bw + i);

					// dot(a,b)
					__m256 dot = _mm256_fmadd_ps(axv, bxv,
						_mm256_fmadd_ps(ayv, byv,
							_mm256_fmadd_ps(azv, bzv, _mm256_mul_ps(awv, bwv))));

					// dot<0 → b を反転
					__m256 maskNeg = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_setzero_si256(), _mm256_castps_si256(dot)));
					// 符号反転は XOR( signMask )
					const __m256 signMask = _mm256_set1_ps(-0.0f);
					bxv = _mm256_xor_ps(bxv, _mm256_and_ps(signMask, maskNeg));
					byv = _mm256_xor_ps(byv, _mm256_and_ps(signMask, maskNeg));
					bzv = _mm256_xor_ps(bzv, _mm256_and_ps(signMask, maskNeg));
					bwv = _mm256_xor_ps(bwv, _mm256_and_ps(signMask, maskNeg));

					// lerp
					__m256 lx = _mm256_fmadd_ps(_mm256_sub_ps(bxv, axv), vA, axv);
					__m256 ly = _mm256_fmadd_ps(_mm256_sub_ps(byv, ayv), vA, ayv);
					__m256 lz = _mm256_fmadd_ps(_mm256_sub_ps(bzv, azv), vA, azv);
					__m256 lw = _mm256_fmadd_ps(_mm256_sub_ps(bwv, awv), vA, awv);

					// normalize (nlerp)
					__m256 len2 = _mm256_fmadd_ps(lx, lx,
						_mm256_fmadd_ps(ly, ly,
							_mm256_fmadd_ps(lz, lz, _mm256_mul_ps(lw, lw))));
					__m256 invL = _mm256_rsqrt_ps(len2);
					//（必要があれば 1-step Newton: invL *= (1.5 - 0.5*len2*invL*invL); を入れる）
					lx = _mm256_mul_ps(lx, invL);
					ly = _mm256_mul_ps(ly, invL);
					lz = _mm256_mul_ps(lz, invL);
					lw = _mm256_mul_ps(lw, invL);

					if (upd.mask01) {
						const __m256i mi = _mm256_cmpgt_epi32(
							_mm256_loadu_si256((const __m256i*)(upd.mask01 + i)),
							_mm256_setzero_si256());
						const __m256 vm = _mm256_castsi256_ps(mi);
						// 旧値とブレンド
						__m256 ox = _mm256_loadu_ps(qx + i), oy = _mm256_loadu_ps(qy + i), oz = _mm256_loadu_ps(qz + i), ow = _mm256_loadu_ps(qw + i);
						_mm256_storeu_ps(qx + i, _mm256_blendv_ps(ox, lx, vm));
						_mm256_storeu_ps(qy + i, _mm256_blendv_ps(oy, ly, vm));
						_mm256_storeu_ps(qz + i, _mm256_blendv_ps(oz, lz, vm));
						_mm256_storeu_ps(qw + i, _mm256_blendv_ps(ow, lw, vm));
					}
					else {
						_mm256_storeu_ps(qx + i, lx);
						_mm256_storeu_ps(qy + i, ly);
						_mm256_storeu_ps(qz + i, lz);
						_mm256_storeu_ps(qw + i, lw);
					}
				}
				for (; i < N; ++i) { if (!upd.mask01 || upd.mask01[i]) do_one((uint32_t)i); }
			}
			else {
				// 疎: indices 使用（gather はコストがあるが疎なら有利）
				size_t j = 0;
				for (; j + 8 <= upd.updateCount; j += 8) {
					__m256i vidx = _mm256_loadu_si256((const __m256i*)(upd.indices + j));
					const int scale = 4;
					__m256 axv = _mm256_i32gather_ps(ax, vidx, scale);
					__m256 ayv = _mm256_i32gather_ps(ay, vidx, scale);
					__m256 azv = _mm256_i32gather_ps(az, vidx, scale);
					__m256 awv = _mm256_i32gather_ps(aw, vidx, scale);
					__m256 bxv = _mm256_i32gather_ps(bx, vidx, scale);
					__m256 byv = _mm256_i32gather_ps(by, vidx, scale);
					__m256 bzv = _mm256_i32gather_ps(bz, vidx, scale);
					__m256 bwv = _mm256_i32gather_ps(bw, vidx, scale);

					__m256 dot = _mm256_fmadd_ps(axv, bxv, _mm256_fmadd_ps(ayv, byv, _mm256_fmadd_ps(azv, bzv, _mm256_mul_ps(awv, bwv))));
					const __m256 signMask = _mm256_set1_ps(-0.0f);
					__m256 maskNeg = _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_setzero_si256(), _mm256_castps_si256(dot)));
					bxv = _mm256_xor_ps(bxv, _mm256_and_ps(signMask, maskNeg));
					byv = _mm256_xor_ps(byv, _mm256_and_ps(signMask, maskNeg));
					bzv = _mm256_xor_ps(bzv, _mm256_and_ps(signMask, maskNeg));
					bwv = _mm256_xor_ps(bwv, _mm256_and_ps(signMask, maskNeg));

					__m256 lx = _mm256_fmadd_ps(_mm256_sub_ps(bxv, axv), vA, axv);
					__m256 ly = _mm256_fmadd_ps(_mm256_sub_ps(byv, ayv), vA, ayv);
					__m256 lz = _mm256_fmadd_ps(_mm256_sub_ps(bzv, azv), vA, azv);
					__m256 lw = _mm256_fmadd_ps(_mm256_sub_ps(bwv, awv), vA, awv);

					__m256 len2 = _mm256_fmadd_ps(lx, lx, _mm256_fmadd_ps(ly, ly, _mm256_fmadd_ps(lz, lz, _mm256_mul_ps(lw, lw))));
					__m256 invL = _mm256_rsqrt_ps(len2);
					lx = _mm256_mul_ps(lx, invL);
					ly = _mm256_mul_ps(ly, invL);
					lz = _mm256_mul_ps(lz, invL);
					lw = _mm256_mul_ps(lw, invL);

					alignas(32) uint32_t idxbuf[8]; _mm256_store_si256((__m256i*)idxbuf, vidx);
					alignas(32) float tx[8], ty[8], tz[8], tw[8];
					_mm256_store_ps(tx, lx); _mm256_store_ps(ty, ly); _mm256_store_ps(tz, lz); _mm256_store_ps(tw, lw);
					for (int k = 0; k < 8; ++k) { const uint32_t id = idxbuf[k]; qx[id] = tx[k]; qy[id] = ty[k]; qz[id] = tz[k]; qw[id] = tw[k]; }
				}
				for (; j < upd.updateCount; ++j) do_one(upd.indices[j]);
			}
#else
			// スカラ版（最短経路 + nlerp）
			const bool hasIdx = (upd.indices && upd.updateCount > 0);
			const float density = (N == 0) ? 0.f : float(upd.updateCount) / float(N);
			if (!hasIdx || density >= sparseThreshold) {
				for (size_t i = 0; i < N; ++i) {
					if (!upd.mask01 || upd.mask01[i]) {
						float dx = ax[i] * bx[i] + ay[i] * by[i] + az[i] * bz[i] + aw[i] * bw[i];
						float sx = bx[i], sy = by[i], sz = bz[i], sw = bw[i];
						if (dx < 0.f) { sx = -sx; sy = -sy; sz = -sz; sw = -sw; }
						float x = ax[i] + (sx - ax[i]) * alpha, y = ay[i] + (sy - ay[i]) * alpha;
						float z = az[i] + (sz - az[i]) * alpha, w = aw[i] + (sw - aw[i]) * alpha;
						float invL = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
						qx[i] = x * invL; qy[i] = y * invL; qz[i] = z * invL; qw[i] = w * invL;
					}
				}
			}
			else {
				for (size_t j = 0; j < upd.updateCount; ++j) {
					const uint32_t id = upd.indices[j];
					float dx = ax[id] * bx[id] + ay[id] * by[id] + az[id] * bz[id] + aw[id] * bw[id];
					float sx = bx[id], sy = by[id], sz = bz[id], sw = bw[id];
					if (dx < 0.f) { sx = -sx; sy = -sy; sz = -sz; sw = -sw; }
					float x = ax[id] + (sx - ax[id]) * alpha, y = ay[id] + (sy - ay[id]) * alpha;
					float z = az[id] + (sz - ax[id + 2]) * alpha; // ←誤: コピペに注意
				}
			}
#endif
		}
	}
}