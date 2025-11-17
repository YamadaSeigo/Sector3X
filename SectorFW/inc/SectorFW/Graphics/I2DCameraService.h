/*****************************************************************//**
 * @file   I2DCameraService.h
 * @brief  2Dカメラサービス（Sprite/UI向け 正射影カメラ）
 * @author seigo
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "RenderTypes.h"
#include "../Core/ECS/ServiceContext.hpp"
#include "../Math/Vector.hpp"
#include "../Math/Matrix.hpp"
#include "../Math/convert.hpp"
#include "../Math/AABB.hpp"

#include <shared_mutex>
#include <numbers>
#include <algorithm>

namespace SFW
{
	namespace Graphics
	{
		/**
		 * @brief 2Dカメラサービスのインターフェース（正射影・回転Z・ズーム）
		 * - 画面＝左上(0,0) / 右下(virtualWidth, virtualHeight) を前提とした座標にも対応
		 * - pixelsPerUnit を用いて世界単位 <-> ピクセル換算を制御
		 * - スクロール（パン）、ズーム、回転Z（シェイク/演出）に対応
		 */
		class I2DCameraService : public ECS::IUpdateService {
			friend class ECS::ServiceLocator;
		public:
			struct CameraBuffer {
				Math::Matrix4x4f view;      // VS用（ワールド→ビュー）
				Math::Matrix4x4f proj;      // VS用（ビュー→クリップ）
				Math::Matrix4x4f viewProj;  // VS用（世界→クリップ）
				// 必要に応じて逆行列など追加可
			};

			explicit I2DCameraService(BufferHandle bufferHandle) noexcept
				: cameraBufferHandle(bufferHandle) {
			}

			virtual ~I2DCameraService() = default;

			// ─────────────────────────────────────────────────────
			// 基本操作
			// ─────────────────────────────────────────────────────

			/// @brief カメラ中心（ワールド座標）を設定
			void SetCenter(const Math::Vec2f& center_) noexcept {
				std::unique_lock lock(sharedMutex);
				center = center_;
				isUpdateBuffer = true;
			}

			/// @brief カメラ中心をオフセット移動（パン）
			void PanBy(const Math::Vec2f& delta) noexcept {
				std::unique_lock lock(sharedMutex);
				center += delta;
				isUpdateBuffer = true;
			}

			/// @brief カメラ回転Z（ラジアン）を設定（画面全体の傾き演出など）
			void SetRotationZ(float radians) noexcept {
				std::unique_lock lock(sharedMutex);
				rotZ = radians;
				isUpdateBuffer = true;
			}

			/// @brief カメラズームを設定（>1で拡大、<1で縮小）
			void SetZoom(float z) noexcept {
				std::unique_lock lock(sharedMutex);
				zoom = (std::max)(z, 1e-4f);
				isUpdateBuffer = true;
			}

			/// @brief 論理解像度（UI基準・設計解像度）を設定
			void SetVirtualResolution(float width, float height) noexcept {
				std::unique_lock lock(sharedMutex);
				virtualWidth = (std::max)(width, 1.0f);
				virtualHeight = (std::max)(height, 1.0f);
				isUpdateBuffer = true;
			}

			/// @brief 世界単位1uが何ピクセルか（PPU）を設定
			void SetPixelsPerUnit(float ppu) noexcept {
				std::unique_lock lock(sharedMutex);
				pixelsPerUnit = (std::max)(ppu, 1e-4f);
				isUpdateBuffer = true;
			}

			/// @brief クリップ面（2Dでもレイヤー用途で残置）
			void SetNearFar(float nearZ, float farZ) noexcept {
				std::unique_lock lock(sharedMutex);
				nearClip = nearZ; farClip = (std::max)(farZ, nearZ + 1e-4f);
				isUpdateBuffer = true;
			}

			// ─────────────────────────────────────────────────────
			// 入力系（マウスホイールでズーム等を想定）
			// ─────────────────────────────────────────────────────
			void AddWheelZoomSteps(int steps, float perStepScale = 1.1f) noexcept {
				if (steps == 0) return;
				std::unique_lock lock(sharedMutex);
				float s = (steps > 0) ? std::pow(perStepScale, float(steps))
					: std::pow(1.0f / perStepScale, float(-steps));
				zoom = std::clamp(zoom * s, 1e-3f, 1e3f);
				isUpdateBuffer = true;
			}

			void Move(const Math::Vec2f& delta) noexcept {
				std::unique_lock lock(sharedMutex);
				moveVec += delta;
				isUpdateBuffer = true;
			}

			void Zoom(float add) {
				std::unique_lock lock(sharedMutex);
				moveZoom += add;
				isUpdateBuffer = true;
			}

			// ─────────────────────────────────────────────────────
			// 取得系
			// ─────────────────────────────────────────────────────
			Math::Vec2f GetCenter() const noexcept {
				std::shared_lock lock(sharedMutex);
				return center;
			}

			float GetRotationZ() const noexcept {
				std::shared_lock lock(sharedMutex);
				return rotZ;
			}

			float GetZoom() const noexcept {
				std::shared_lock lock(sharedMutex);
				return zoom;
			}

			Math::Vec2f GetVirtualResolution() const noexcept {
				std::shared_lock lock(sharedMutex);
				return { virtualWidth, virtualHeight };
			}

			float GetPixelsPerUnit() const noexcept {
				std::shared_lock lock(sharedMutex);
				return pixelsPerUnit;
			}

			float GetNear() const noexcept { std::shared_lock lock(sharedMutex); return nearClip; }
			float GetFar() const noexcept { std::shared_lock lock(sharedMutex); return farClip; }

			const CameraBuffer& GetCameraBufferData() const noexcept { return cameraBuffer[currentSlot]; }

			// ─────────────────────────────────────────────────────
			// 座標変換ユーティリティ
			// （virtual 解像度のスクリーン座標 ↔ ワールド座標）
			// ─────────────────────────────────────────────────────

			/// @brief virtual解像度でのスクリーン座標(x,y)→ワールド座標
			Math::Vec2f ScreenToWorld(const Math::Vec2f& screen) const noexcept {
				std::shared_lock lock(sharedMutex);
				// スクリーン→NDC（左上(0,0)、右下(w,h)）
				float ndcX = (screen.x / virtualWidth) * 2.0f - 1.0f;
				float ndcY = -(screen.y / virtualHeight) * 2.0f + 1.0f;

				// 逆行列（viewProj の逆）で同次座標をワールドへ
				Math::Matrix4x4f inv = cameraBufferInv;
				Math::Vec4f p = inv * Math::Vec4f{ ndcX, ndcY, 0.0f, 1.0f };
				if (std::abs(p.w) > 1e-6f) { p.x /= p.w; p.y /= p.w; }
				return { p.x, p.y };
			}

			/// @brief ワールド座標→virtual解像度のスクリーン座標(x,y)
			Math::Vec2f WorldToScreen(const Math::Vec2f& world) const noexcept {
				std::shared_lock lock(sharedMutex);
				Math::Vec4f p = cameraBuffer[currentSlot].viewProj * Math::Vec4f{world.x, world.y, 0.0f, 1.0f};
				if (std::abs(p.w) > 1e-6f) { p.x /= p.w; p.y /= p.w; }
				// NDC→スクリーン
				float sx = (p.x * 0.5f + 0.5f) * virtualWidth;
				float sy = (-p.y * 0.5f + 0.5f) * virtualHeight;
				return { sx, sy };
			}

			/// @brief 現在のビューで可視なワールド範囲（Zは[near,far]）を返す
			Math::AABB<float, Math::Vec2f> MakeViewAABB() const noexcept {
				// スクリーン四隅をワールドに戻す
				Math::Vec2f tl = ScreenToWorld({ 0.0f,           0.0f });
				Math::Vec2f br = ScreenToWorld({ virtualWidth,   virtualHeight });
				Math::Vec2f lb = { (std::min)(tl.x, br.x), (std::min)(tl.y, br.y) };
				Math::Vec2f ub = { (std::max)(tl.x, br.x), (std::max)(tl.y, br.y) };
				return { lb, ub };
			}

			// 行列計算（内部専用）
			void RecomputeMatrices_NoLock(uint16_t slot) noexcept {
				// 1) 正射影行列（左上原点のピクセル直感に寄せるため、virtual解像度基準）
				//    世界→スクリーンを素直にするため、世界単位→ピクセル換算(pixelsPerUnit)とzoomを投影に反映
				//    ここでは「世界座標系」をそのまま使い、viewで中心や回転・ズームを扱う構成にする。
				//    正射影は -1..+1 に線形写像するだけなので、OffCenterLH を使う。
				//
				// 可視幅/高さ（世界単位）: virtualWidth/Height を PPU と zoom で割り戻す
				const float worldW = (virtualWidth / pixelsPerUnit) / zoom;
				const float worldH = (virtualHeight / pixelsPerUnit) / zoom;

				// 中心 center を基準に「左上/右下」の世界座標を決定
				const float left = center.x - worldW * 0.5f;
				const float right = center.x + worldW * 0.5f;
				const float top = center.y + worldH * 0.5f;
				const float bottom = center.y - worldH * 0.5f;

				cameraBuffer[slot].proj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(
					left, right, bottom, top, nearClip, farClip);

				// 2) View 行列
				//    2Dの一般的なカメラは T(-center) → Rz(-rotZ) で十分
				Math::Matrix4x4f T = Math::MakeTranslationMatrix(Math::Vec3f{ -center.x, -center.y, 0.0f });
				Math::Matrix4x4f R = Math::MakeRotationMatrix(Math::Quatf::FromEuler(0.0f, 0.0f, -rotZ));

				cameraBuffer[slot].view = T * R;

				cameraBuffer[slot].viewProj = cameraBuffer[slot].proj * cameraBuffer[slot].view;

				// 逆行列（座標変換ユーティリティ用）
				cameraBufferInv = Math::Inverse(cameraBuffer[slot].viewProj); // 実装がなければ適宜追加
			}

		private:
			// 毎フレーム or 変更時にバッファ更新
			virtual void Update(double /*deltaTime*/) override {
				++frameIdx;

				if (!isUpdateBuffer) return;
				std::unique_lock lock(sharedMutex);

				currentSlot = frameIdx % RENDER_BUFFER_COUNT;
				RecomputeMatrices_NoLock(currentSlot);
				moveVec = Math::Vec2f{ 0.0f, 0.0f };
				isUpdateBuffer = false;
			}

		protected:
			// GPU 連携
			BufferHandle  cameraBufferHandle;
			size_t frameIdx{ 0 };
			uint16_t currentSlot{ 0 };
			CameraBuffer  cameraBuffer[RENDER_BUFFER_COUNT] {};
			Math::Matrix4x4f cameraBufferInv{ Math::Matrix4x4f::Identity() };

			// 2D カメラ状態
			Math::Vec2f center{ 0.0f, 0.0f };       // 画面中央に対応するワールド座標
			float       rotZ{ 0.0f };             // 画面の傾き（rad）
			float       zoom{ 1.0f };             // >1 拡大、<1 縮小


			Math::Vec2f moveVec{ 0.0f, 0.0f };   // 移動ベクトル（Updateで加算）
			float moveZoom{ 0.0f };           // ズーム倍率（Updateで乗算）

			// 表示系パラメータ
			float virtualWidth{ 1920.0f };         // 論理解像度（UI/設計解像度）
			float virtualHeight{ 1080.0f };
			float pixelsPerUnit{ 1.0f };          // 世界単位1u = 1px 相当

			// 2Dでもレイヤー/深度ソート用途で残置
			float nearClip{ 0.0f };
			float farClip{ 1.0f };

			// 同期と更新判定
			mutable std::shared_mutex sharedMutex;
			bool isUpdateBuffer{ true };

		public:
			STATIC_SERVICE_TAG
		};
	}
}
