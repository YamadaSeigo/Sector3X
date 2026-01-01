/*****************************************************************//**
 * @file   WinInput.h
 * @brief Windows向けの入力デバイスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "InputDevice.hpp"
#include <array>

#include "WinMouseInput.h"

namespace SFW
{
	namespace Input
	{
		/**
		 * @brief Windows向けの入力デバイス
		 */
		class WinInput : public InputDevice<WinInput> {
			/**
			 * @brief Windows VK Key Mapping
			 */
			constexpr static inline std::array<Key, 256> keyToCommandKey = [] {
				std::array<Key, 256> map{};
				map[0x00] = Key::Unknown; // No key
				map[0x01] = Key::LBUTTON; // Left mouse button
				map[0x02] = Key::RBUTTON; // Right mouse button
				map[0x03] = Key::CANCEL; // Cancel key
				map[0x04] = Key::MBUTTON; // Middle mouse button
				map[0x41] = Key::A;
				map[0x42] = Key::B;
				map[0x43] = Key::C;
				map[0x44] = Key::D;
				map[0x45] = Key::E;
				map[0x46] = Key::F;
				map[0x47] = Key::G;
				map[0x48] = Key::H;
				map[0x49] = Key::I;
				map[0x4A] = Key::J;
				map[0x4B] = Key::K;
				map[0x4C] = Key::L;
				map[0x4D] = Key::M;
				map[0x4E] = Key::N;
				map[0x4F] = Key::O;
				map[0x50] = Key::P;
				map[0x51] = Key::Q;
				map[0x52] = Key::R;
				map[0x53] = Key::S;
				map[0x54] = Key::T;
				map[0x55] = Key::U;
				map[0x56] = Key::V;
				map[0x57] = Key::W;
				map[0x58] = Key::X;
				map[0x59] = Key::Y;
				map[0x5A] = Key::Z;
				map[0x30] = Key::Num0;
				map[0x31] = Key::Num1;
				map[0x32] = Key::Num2;
				map[0x33] = Key::Num3;
				map[0x34] = Key::Num4;
				map[0x35] = Key::Num5;
				map[0x36] = Key::Num6;
				map[0x37] = Key::Num7;
				map[0x38] = Key::Num8;
				map[0x39] = Key::Num9;
				map[VK_ESCAPE] = Key::Escape;
				map[VK_RETURN] = Key::Enter;
				map[VK_TAB] = Key::Tab;
				map[VK_BACK] = Key::Backspace;
				map[VK_SPACE] = Key::Space;
				map[VK_LEFT] = Key::Left;
				map[VK_RIGHT] = Key::Right;
				map[VK_UP] = Key::Up;
				map[VK_DOWN] = Key::Down;
				map[VK_F1] = Key::F1;
				map[VK_F2] = Key::F2;
				map[VK_F3] = Key::F3;
				map[VK_F4] = Key::F4;
				map[VK_F5] = Key::F5;
				map[VK_F6] = Key::F6;
				map[VK_F7] = Key::F7;
				map[VK_F8] = Key::F8;
				map[VK_F9] = Key::F9;
				map[VK_F10] = Key::F10;
				map[VK_F11] = Key::F11;
				map[VK_F12] = Key::F12;
				map[VK_LSHIFT] = Key::LShift;
				map[VK_RSHIFT] = Key::RShift;
				map[VK_LCONTROL] = Key::LCtrl;
				map[VK_RCONTROL] = Key::RCtrl;
				map[VK_LMENU] = Key::LAlt;
				map[VK_RMENU] = Key::RAlt;
				return map;
				}();

			/**
			 * @brief Key to WinAPI VK Mapping
			 */
			constexpr static inline std::array<int, static_cast<size_t>(Key::Count)> KeyToVKMap = [] {
				std::array<int, static_cast<size_t>(Key::Count)> map{};
				map[static_cast<size_t>(Key::Unknown)] = 0;
				map[static_cast<size_t>(Key::LBUTTON)] = 0x01; // Left mouse button
				map[static_cast<size_t>(Key::RBUTTON)] = 0x02; // Right mouse button
				map[static_cast<size_t>(Key::CANCEL)] = 0x03; // Cancel key
				map[static_cast<size_t>(Key::MBUTTON)] = 0x04; // Middle mouse button
				map[static_cast<size_t>(Key::A)] = 0x41;
				map[static_cast<size_t>(Key::B)] = 0x42;
				map[static_cast<size_t>(Key::C)] = 0x43;
				map[static_cast<size_t>(Key::D)] = 0x44;
				map[static_cast<size_t>(Key::E)] = 0x45;
				map[static_cast<size_t>(Key::F)] = 0x46;
				map[static_cast<size_t>(Key::G)] = 0x47;
				map[static_cast<size_t>(Key::H)] = 0x48;
				map[static_cast<size_t>(Key::I)] = 0x49;
				map[static_cast<size_t>(Key::J)] = 0x4A;
				map[static_cast<size_t>(Key::K)] = 0x4B;
				map[static_cast<size_t>(Key::L)] = 0x4C;
				map[static_cast<size_t>(Key::M)] = 0x4D;
				map[static_cast<size_t>(Key::N)] = 0x4E;
				map[static_cast<size_t>(Key::O)] = 0x4F;
				map[static_cast<size_t>(Key::P)] = 0x50;
				map[static_cast<size_t>(Key::Q)] = 0x51;
				map[static_cast<size_t>(Key::R)] = 0x52;
				map[static_cast<size_t>(Key::S)] = 0x53;
				map[static_cast<size_t>(Key::T)] = 0x54;
				map[static_cast<size_t>(Key::U)] = 0x55;
				map[static_cast<size_t>(Key::V)] = 0x56;
				map[static_cast<size_t>(Key::W)] = 0x57;
				map[static_cast<size_t>(Key::X)] = 0x58;
				map[static_cast<size_t>(Key::Y)] = 0x59;
				map[static_cast<size_t>(Key::Z)] = 0x5A;
				map[static_cast<size_t>(Key::Num0)] = 0x30;
				map[static_cast<size_t>(Key::Num1)] = 0x31;
				map[static_cast<size_t>(Key::Num2)] = 0x32;
				map[static_cast<size_t>(Key::Num3)] = 0x33;
				map[static_cast<size_t>(Key::Num4)] = 0x34;
				map[static_cast<size_t>(Key::Num5)] = 0x35;
				map[static_cast<size_t>(Key::Num6)] = 0x36;
				map[static_cast<size_t>(Key::Num7)] = 0x37;
				map[static_cast<size_t>(Key::Num8)] = 0x38;
				map[static_cast<size_t>(Key::Num9)] = 0x39;
				map[static_cast<size_t>(Key::Escape)] = VK_ESCAPE;
				map[static_cast<size_t>(Key::Enter)] = VK_RETURN;
				map[static_cast<size_t>(Key::Tab)] = VK_TAB;
				map[static_cast<size_t>(Key::Backspace)] = VK_BACK;
				map[static_cast<size_t>(Key::Space)] = VK_SPACE;
				map[static_cast<size_t>(Key::Left)] = VK_LEFT;
				map[static_cast<size_t>(Key::Right)] = VK_RIGHT;
				map[static_cast<size_t>(Key::Up)] = VK_UP;
				map[static_cast<size_t>(Key::Down)] = VK_DOWN;
				map[static_cast<size_t>(Key::F1)] = VK_F1;
				map[static_cast<size_t>(Key::F2)] = VK_F2;
				map[static_cast<size_t>(Key::F3)] = VK_F3;
				map[static_cast<size_t>(Key::F4)] = VK_F4;
				map[static_cast<size_t>(Key::F5)] = VK_F5;
				map[static_cast<size_t>(Key::F6)] = VK_F6;
				map[static_cast<size_t>(Key::F7)] = VK_F7;
				map[static_cast<size_t>(Key::F8)] = VK_F8;
				map[static_cast<size_t>(Key::F9)] = VK_F9;
				map[static_cast<size_t>(Key::F10)] = VK_F10;
				map[static_cast<size_t>(Key::F11)] = VK_F11;
				map[static_cast<size_t>(Key::F12)] = VK_F12;
				map[static_cast<size_t>(Key::LCtrl)] = VK_LCONTROL;
				map[static_cast<size_t>(Key::RCtrl)] = VK_RCONTROL;
				map[static_cast<size_t>(Key::LShift)] = VK_LSHIFT;
				map[static_cast<size_t>(Key::RShift)] = VK_RSHIFT;
				map[static_cast<size_t>(Key::LAlt)] = VK_LMENU;
				map[static_cast<size_t>(Key::RAlt)] = VK_RMENU;
				return map;
				}();

			/**
			 * @brief Lookup Function
			 */
			inline int GetVKFromKey(Key key) const noexcept {
				return KeyToVKMap[static_cast<size_t>(key)];
			}

		public:
			/**
			 * @brief コンストラクタ
			 * @param mouseInput マウス入力デバイスのポインタ
			 */
			explicit WinInput(WinMouseInput* mouseInput) noexcept
				: mouseInput(mouseInput) {
			}
			/**
			 * @brief 入力状態を更新する関数
			 */
			void UpdateImpl() {
				memcpy(oldKeyStates, keyStates, sizeof(keyStates));

				BOOL result = GetKeyboardState(keyStates);
				if (!result) {
					assert(false && "Failed to get keyboard state");
				}
			}
			/**
			 * @brief キーが押されているかを確認する関数
			 * @param key 確認するキー
			 * @return bool 押されている場合はtrue、押されていない場合はfalse
			 */
			bool IsKeyPressedImpl(Key key) const noexcept {
				return keyStates[GetVKFromKey(key)] & 0x80;
			}
			/**
			 * @brief キーが離されたかを確認する関数
			 * @param key 確認するキー
			 * @return bool 離された場合はtrue、離されていない場合はfalse
			 */
			bool IsKeyReleasedImpl(Key key) const noexcept {
				auto keyIndex = GetVKFromKey(key);
				return !(keyStates[keyIndex] & 0x80) && (oldKeyStates[keyIndex] & 0x80);
			}
			/**
			 * @brief キーがトリガーされたかを確認する関数
			 * @param key 確認するキー
			 * @return bool トリガーされた場合はtrue、トリガーされていない場合はfalse
			 */
			bool IsKeyTriggerImpl(Key key) const noexcept {
				auto keyIndex = GetVKFromKey(key);
				return (keyStates[keyIndex] & 0x80) && !(oldKeyStates[keyIndex] & 0x80);
			}
			/**
			 * @brief 左ボタンが押されているかを確認する関数
			 * @return bool 押されている場合はtrue、押されていない場合はfalse
			 */
			bool IsLButtonPressedImpl() const noexcept {
				return mouseInput->IsLeftDown();
			}
			/**
			 * @brief 右ボタンが押されているかを確認する関数
			 * @return bool 押されている場合はtrue、押されていない場合はfalse
			 */
			bool IsRButtonPressedImpl() const noexcept {
				return mouseInput->IsRightDown();
			}
			/**
			 * @brief ミドルボタンが押されているかを確認する関数
			 * @return bool 押されている場合はtrue、押されていない場合はfalse
			 */
			bool IsMouseCapturedImpl() const noexcept {
				return mouseInput->IsCaptured();
			}
			/**
			 * @brief マウスの移動量を取得する関数
			 * @param outDx マウスの移動量X
			 * @param outDy　マウスの移動量Y
			 */
			void GetMouseDeltaImpl(long& outDx, long& outDy) const noexcept {
				mouseInput->GetDelta(outDx, outDy);
			}
			/**
			 * @brief マウスのホイールの回転量を取得する関数
			 * @param outWheelV マウスのホイールの回転量垂直
			 * @param outWheelH マウスのホイールの回転量水平
			 */
			void GetMouseWheelImpl(int& outWheelV, int& outWheelH) const noexcept {
				mouseInput->GetMouseWheel(outWheelV, outWheelH);
			}
		private:
			BYTE keyStates[256] = { 0 };
			BYTE oldKeyStates[256] = { 0 };

			WinMouseInput* mouseInput;
		};
	}
}
