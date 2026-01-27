/*****************************************************************//**
 * @file   PhysicsService.h
 * @brief 物理サービスを定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include "PhysicsTypes.h"
#include "PhysicsSnapshot.h"
#include "PhysicsDevice.h"
#include "PhysicsShapeManager.h"
#include "../Util/SpscRing.hpp"
#include "../Core/ECS/ServiceContext.hpp"
#include "../Core/RegistryTypes.h"

namespace SFW
{
	namespace Physics
	{
		/**
		 * @brief 物理サービスを提供するクラス。systemから物理Deviceを操作するためのAPIを提供する
		 */
		class PhysicsService : public ECS::IUpdateService {
		public:
			struct Plan {
				float fixed_dt = 1.0f / 60.0f;
				int   substeps = 1;
				bool  collect_debug = false; // 後でデバッグライン等を拾う用
			};

			/**
			 * @brief 生成インテント：発生源で「この Entity を作って」と積んでおく
			 */
			struct CreateIntent {
				Entity     e;
				ShapeHandle h;
				SpatialChunkKey owner;
			};
			/**
			 * @brief コンストラクタ
			 * @param device 物理デバイス
			 * @param shapeMgr 形状マネージャー
			 * @param plan 物理シミュレーションの計画（固定時間ステップ等）
			 * @param queueCapacityPow2 コマンドキューの容量（2の累乗、デフォルトは4096）
			 */
			explicit PhysicsService(PhysicsDevice& device, PhysicsShapeManager& shapeMgr,
				Plan plan = { 1.0f / 60.0f, 1, false }, size_t queueCapacityPow2 = 4096)
				: m_device(device), m_mgr(&shapeMgr), plan(plan), m_queue(queueCapacityPow2) {
				m_device.SetShapeResolver(m_mgr);
			}
			/**
			 * @brief 指定した形状を生成する
			 * @param desc 形状生成記述子
			 * @return ShapeHandle 生成された形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeShape(const ShapeCreateDesc& desc) {
				ShapeHandle h; m_mgr->Add(desc, h); return h;
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
			/**
			 * @brief 球形状を生成する
			 * @param radius 球の半径
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @return ShapeHandle 生成された球形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeSphere(float radius, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ SphereDesc{radius}, s }, h); return h;
			}
			/**
			 * @brief カプセル形状を生成する
			 * @param halfHeight 半分の高さ
			 * @param radius 球の半径
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @return ShapeHandle 生成された球形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeCapsule(float halfHeight, float radius, ShapeScale s = { {1,1,1} })
			{
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ CapsuleDesc{halfHeight, radius}, s }, h); return h;
			}

			/**
			 * @brief メッシュ形状を生成する
			 * @param vertex 頂点群
			 * @param indices 頂点インデックス群
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @return ShapeHandle 生成されたメッシュ形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeMesh(const std::vector<Vec3f>& vertex, const std::vector<uint32_t>& indices, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ MeshDesc{vertex, indices}, s }, h); return h;
			}

			/**
			 * @brief メッシュ形状を生成する（ファイル読み込み版）
			 * @param path メッシュファイルのパス
			 * @param rhFlip 右手系変換が必要なら true
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @return ShapeHandle 生成されたメッシュ形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeMesh(const std::string& path, bool rhFlip = false, ShapeScale s = { {1,1,1} }) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ MeshFileDesc{path, rhFlip}, s }, h); return h;
			}

			/**
			 * @brief 凸形状を生成する
			 * @param pts 凸形状を作成する頂点群
			 * @param idx 頂点インデックス群
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @param r  シュリンク半径（デフォルトは 0.05f）
			 * @param tol 許容誤差（デフォルトは 0.005f）
			 * @return ShapeHandle 生成されたカプセル形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeConvex(const std::vector<Vec3f>& pts, const std::vector<uint32_t>& idx, float r = 0.05f, float tol = 0.005f) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ ConvexHullDesc{pts, idx, r, tol}, {{1.0f,1.0f,1.0f}} }, h); return h;
			}

			/**
			 * @brief 凸形状群から StaticCompoundShape を生成する
			 * @param hulls 凸形状群
			 * @param rhFlip 右手系変換が必要なら true
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @param r  シュリンク半径（デフォルトは 0.05f）
			 * @param tol 許容誤差（デフォルトは 0.005f）
			 * @return ShapeHandle 生成された StaticCompoundShape 形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeConvexCompound(const std::vector<VHACDHull>& hulls, bool rhFlip = false, ShapeScale s = { {1,1,1} }, float r = 0.05f, float tol = 0.005f) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ ConvexCompoundDesc{hulls, r, tol, rhFlip}, s }, h); return h;
			}

			/**
			 * @brief VHACD バイナリファイルから凸形状群を読み込み、StaticCompoundShape を生成する
			 * @param path 凸形状バイナリファイルのパス
			 * @param rhFlip 右手系変換が必要なら true
			 * @param s スケール（デフォルトは {1,1,1}）
			 * @param r  シュリンク半径（デフォルトは 0.05f）
			 * @param tol 許容誤差（デフォルトは 0.005f）
			 * @return ShapeHandle 生成された StaticCompoundShape 形状のハンドル
			 */
			[[nodiscard]] ShapeHandle MakeConvexCompound(const std::string& path, bool rhFlip = false, ShapeScale s = { {1,1,1} }, float r = 0.05f, float tol = 0.005f) {
				ShapeHandle h; m_mgr->Add(ShapeCreateDesc{ ConvexCompoundFileDesc{path, r, tol, rhFlip}, s }, h); return h;
			}

			/**
			 * @brief 形状を解放する
			 * @param h 解放する形状のハンドル
			 * @param sync 同期用のシグナル（デフォルトは0、未使用
			 */
			void ReleaseShape(ShapeHandle h, uint64_t sync = 0) { m_mgr->Release(h, sync); }

			// ====== ゲーム側 API（コマンドを積むだけ）======
			/**
			 * @brief 物理ボディを作成するコマンドをキューに追加する
			 * @param c 作成コマンド
			 */
			bool CreateBody(const CreateBodyCmd& c) { return Enqueue(c); }
			/**
			 * @brief 物理ボディを破棄するコマンドをキューに追加する
			 * @param e 破棄するエンティティ
			 */
			bool DestroyBody(Entity e) { return Enqueue(DestroyBodyCmd{ e }); }
			/**
			 * @brief 物理ボディをテレポートするコマンドをキューに追加する
			 * @param e テレポートするエンティティ
			 * @param tm テレポート先の変換行列
			 * @param wake テレポート後に起こすかどうか（デフォルトは true）
			 */
			bool Teleport(Entity e, const Mat34f& tm, bool wake = true) { return Enqueue(TeleportCmd{ e, wake, tm }); }
			/**
			 * @brief 物理ボディの線形速度を設定するコマンドをキューに追加する
			 * @param e 設定するエンティティ
			 * @param v 設定する線形速度（Vec3f）
			 */
			bool SetLinearVelocity(Entity e, Vec3f v) { return Enqueue(SetLinearVelocityCmd{ e, v }); }
			/**
			 * @brief 物理ボディの角速度を設定するコマンドをキューに追加する
			 * @param e 設定するエンティティ
			 * @param w 設定する角速度（Vec3f）
			 */
			bool SetAngularVelocity(Entity e, Vec3f w) { return Enqueue(SetAngularVelocityCmd{ e, w }); }
			/**
			 * @brief 物理ボディにインパルスを加えるコマンドをキューに追加する
			 * @param e インパルスを加えるエンティティ
			 * @param p 加えるインパルス（Vec3f）
			 * @param at インパルスを加える位置（ワールド座標、デフォルトはボディ中心）
			 */
			bool AddImpulse(Entity e, Vec3f p, std::optional<Vec3f> at = std::nullopt) {
				AddImpulseCmd cmd{ e, p, {}, false };
				if (at) { cmd.atWorldPos = *at; cmd.useAtPos = true; }
				return Enqueue(cmd);
			}
			/**
			 * @brief 物理ボディにトルクを加えるコマンドをキューに追加する
			 * @param e トルクを加えるエンティティ
			 * @param tm 加えるトルク（Vec3f）
			 */
			bool SetKinematicTarget(Entity e, const Mat34f& tm) { return Enqueue(SetKinematicTargetCmd{ e, tm }); }
			/**
			 * @brief 物理ボディの衝突マスクを設定するコマンドをキューに追加する
			 * @param e 設定するエンティティ
			 * @param mask 設定する衝突マスク
			 */
			bool SetCollisionMask(Entity e, uint32_t mask) { return Enqueue(SetCollisionMaskCmd{ e, mask }); }
			/**
			 * @brief 物理ボディのレイヤーを設定するコマンドをキューに追加する
			 * @param e 設定するエンティティ
			 * @param layer レイヤー
			 * @param broad ブロードフェーズレイヤー
			 */
			bool SetObjectLayer(Entity e, uint16_t layer, uint16_t broad) { return Enqueue(SetObjectLayerCmd{ e, layer, broad }); }
			/**
			 * @brief レイキャストを実行するコマンドをキューに追加する
			 * @param reqId リクエストID（応答時に返される）
			 * @param o 開始位置（Vec3f）
			 * @param dir 方向ベクトル（正規化されていること、Vec3f）
			 * @param maxDist 最大距離
			 */
			bool RayCast(uint32_t reqId, Vec3f o, Vec3f dir, float maxDist) { return Enqueue(RayCastCmd{ reqId, o, dir, maxDist }); }

			/**
			 * @brief レイキャストを実行するコマンドをキューに追加する
			 * @param c レイキャストコマンド
			 */
			bool RayCast(const RayCastCmd& c) {
				return Enqueue(c);
			}

			bool CreateCharacter(const CreateCharacterCmd& c) {
				return Enqueue(c);
			}

			bool SetCharacterVelocity(Entity e, Vec3f v) {
				return Enqueue(SetCharacterVelocityCmd{ e, v });
			}

			bool SetCharacterRotation(Entity e, const Quatf& q) {
				return Enqueue(SetCharacterRotationCmd{ e, q });
			}

			bool TeleportCharacter(Entity e, const Mat34f& tm) {
				return Enqueue(TeleportCharacterCmd{ e, tm });
			}

			bool DestroyCharacter(Entity e) {
				return Enqueue(DestroyCharacterCmd{ e });
			}

			std::optional<CharacterPose> ReadCharacterPose(Entity e) {
				return m_device.GetCharacterPose(e);
			}

			/**
			 * @brief 物理シミュレーションを更新する（IUpdateService 実装）
			 * @param dt 可変フレーム時間（ゲームループから呼ぶ）
			 */
			void PreUpdate(double dt) override {
				m_accum += static_cast<float>(dt);

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
			/**
			 * @brief ポーズバッチを構築する（描画フレームで呼ぶ）
			 * @param v ポーズバッチビューの参照
			 */
			void BuildPoseBatch(PoseBatchView& v) const {
				m_device.ReadPosesBatch(v);
			}
			/**
			 * @brief 補間に使うための α を取得（描画フレームで使う）
			 * @return float 補間係数 α (0.0 ～ 1.0)
			 */
			float GetAlpha() const {
				return (plan.fixed_dt > 0.0f) ? (m_accum / plan.fixed_dt) : 0.0f;
			}

			// 現在（最後の fixed step 後）のスナップショット参照を返す
			const PhysicsSnapshot& CurrentSnapshot() const { return m_currSnapshot; }
			const PhysicsSnapshot& PreviousSnapshot() const { return m_prevSnapshot; }

			/**
			 * @brief 生成済み BodyID の参照（差し込み用
			 * @param e エンティティ
			 * @return std::optional<JPH::BodyID> 生成済み BodyID（存在しない場合は std::nullopt
			 */
			std::optional<JPH::BodyID> TryGetBodyID(Entity e) const noexcept {
				return m_device.TryGetBodyID(e);
			}
			/**
			 * @brief 生成インテントをキューに追加する（Body 作成要求用
			 * @param e 対象のエンティティ
			 * @param h 形状ハンドル
			 * @param owner 所有チャンクキー
			 */
			void EnqueueCreateIntent(Entity e, ShapeHandle h, const SpatialChunkKey& owner) {
				std::scoped_lock lk(m_intentMutex);
				m_createIntents.push_back({ e, h, owner });
			}
			/**
			 * @brief 生成インテントを取り出す（Body 作成要求用
			 * @param out 取り出し先のベクター
			 */
			void SwapCreateIntents(std::vector<CreateIntent>& out) {
				std::scoped_lock lk(m_intentMutex);
				out.swap(m_createIntents); // O(1)
			}
			/**
			 * @brief Body 作成完了イベントの取り出し（WriteBack用）
			 */
			struct CreatedBody { Entity e; SpatialChunkKey owner; JPH::BodyID id; };
			/**
			 * @brief 生成済み Body イベントを取り出す
			 * @param out 取り出し先のベクター
			 */
			void ConsumeCreatedBodies(std::vector<CreatedBody>& out) {
				std::vector<PhysicsDevice::CreatedBody> tmp;
				m_device.ConsumeCreatedBodies(tmp);
				out.clear(); out.reserve(tmp.size());
				for (auto& x : tmp) out.push_back(CreatedBody{ x.e, x.owner, x.id });
			}
			/**
			 * @brief 形状マネージャーの参照を取得する
			 * @return const PhysicsShapeManager* 形状マネージャーの参照
			 */
			const PhysicsShapeManager* GetShapeManager() const noexcept { return m_mgr; }
			/**
			 * @brief 形状の寸法を取得する
			 * @param h 形状ハンドル
			 * @return std::optional<ShapeDims> 形状の寸法（存在しない場合は std::nullopt
			 */
			std::optional<ShapeDims> GetShapeDims(ShapeHandle h) const {
				auto shape = m_mgr->Resolve(h);
				return m_mgr->GetShapeDims(shape, h);
			}

#ifdef CACHE_SHAPE_WIRE_DATA
			/**
			 * @brief 形状のワイヤーフレームデータを取得する
			 * @param h 形状ハンドル
			 * @return std::optional<WireframeData> ワイヤーフレームデータ（存在しない場合は std::nullopt
			 */
			std::optional<ShareWireframeData> GetShapeWireframeData(ShapeHandle h) const {
				return m_mgr->GetShapeWireframeData(h);
			}
#endif
		private:
			template<class T>
			bool Enqueue(const T& c) {
				const PhysicsCommand& cmd = c;
				// 失敗（満杯）の場合はリトライ or 一時的にブロッキングに切替など、運用ポリシー次第
				return m_queue.push(cmd);
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
				DEFINE_UPDATESERVICE_GROUP(GROUP_PHYSICS)
		};
	}
}