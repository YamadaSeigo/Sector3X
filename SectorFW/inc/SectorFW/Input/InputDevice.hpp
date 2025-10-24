/*****************************************************************//**
 * @file   InputDevice.hpp
 * @brief 入力デバイスの抽象クラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../Core/ECS/ServiceContext.hpp"

namespace SFW
{
	namespace Input
	{
		/**
		 * @brief Key Enum for InputDevice
		 */
		enum class Key : uint16_t {
			LBUTTON,
			RBUTTON,
			CANCEL,
			MBUTTON,
			Unknown,
			A,
			B,
			C,
			D,
			E,
			F,
			G,
			H,
			I,
			J,
			K,
			L,
			M,
			N,
			O,
			P,
			Q,
			R,
			S,
			T,
			U,
			V,
			W,
			X,
			Y,
			Z,
			Num0,
			Num1,
			Num2,
			Num3,
			Num4,
			Num5,
			Num6,
			Num7,
			Num8,
			Num9,
			Escape,
			Enter,
			Tab,
			Backspace,
			Space,
			Left,
			Right,
			Up,
			Down,
			LShift,
			RShift,
			LCtrl,
			RCtrl,
			LAlt,
			RAlt,
			Count
		};
		/**
		 * @brief 入力デバイスの抽象クラス。非環境依存のために抽象化。ServiceLocatorで管理される。
		 */
		template<typename Derived>
		class InputDevice : public ECS::IUpdateService
		{
			friend class ServiceLocator;

		public:
			/**
			 * @brief キーが押されているかどうかを取得します。
			 * @param key チェックするキー
			 * @return bool 押されている場合はtrue、そうでない場合はfalse
			 */
			bool IsKeyPressed(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyPressedImpl(key);
			}
			/**
			 * @brief キーが離されたかどうかを取得します。
			 * @param key チェックするキー
			 * @return bool 離された場合はtrue、そうでない場合はfalse
			 */
			bool IsKeyReleased(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyReleasedImpl(key);
			}
			/**
			 * @brief キーがトリガーされたかどうかを取得します。
			 * @param key チェックするキー
			 * @return bool トリガーされた場合はtrue、そうでない場合はfalse
			 */
			bool IsKeyTrigger(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyTriggerImpl(key);
			}
			/**
			 * @brief 左ボタンが押されているかどうかを取得します。
			 * @return bool 押されている場合はtrue、そうでない場合はfalse
			 */
			bool IsLButtonPressed() const {
				return static_cast<const Derived*>(this)->IsLButtonPressedImpl();
			}
			/**
			 * @brief 右ボタンが押されているかどうかを取得します。
			 * @return bool 押されている場合はtrue、そうでない場合はfalse
			 */
			bool IsRButtonPressed() const {
				return static_cast<const Derived*>(this)->IsRButtonPressedImpl();
			}
			/**
			 * @brief マウスの入力がキャプチャされているかどうかを取得します。
			 * @return bool キャプチャされている場合はtrue、そうでない場合はfalse
			 */
			bool IsMouseCaptured() const {
				return static_cast<const Derived*>(this)->IsMouseCapturedImpl();
			}
			/**
			 * @brief マウスの入力をキャプチャするかどうかを設定します。
			 * @param captured キャプチャする場合はtrue、そうでない場合はfalse
			 */
			void SetMouseCaptured(bool captured) {
				static_cast<Derived*>(this)->SetMouseCapturedImpl(captured);
			}
			/**
			 * @brief マウスの位置を取得します。
			 * @param outDx マウスのX座標
			 * @param outDy　マウスのY座標
			 */
			void GetMouseDelta(long& outDx, long& outDy) const noexcept {
				static_cast<const Derived*>(this)->GetMouseDeltaImpl(outDx, outDy);
			}
			/**
			 * @brief マウスホイールの状態を取得します。
			 * @param outWheelV 垂直方向のホイールの状態
			 * @param outWheelH 水平方向のホイールの状態
			 */
			void GetMouseWheel(int& outWheelV, int& outWheelH) const noexcept {
				static_cast<const Derived*>(this)->GetMouseWheelImpl(outWheelV, outWheelH);
			}
		private:
			void Update(double deltaTime) override {
				static_cast<Derived*>(this)->UpdateImpl();
			}
		public:
			STATIC_SERVICE_TAG
		};
	}
}