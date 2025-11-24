/*****************************************************************//**
 * @file   WinMouseInput.h
 * @brief Windows向けマウス入力処理クラスのヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <windows.h>
#include <hidusage.h>
#include <vector>

namespace SFW
{
	class WindowHandler;

	namespace Input
	{
		/**
		 * @brief Windows向けマウス入力処理クラス
		 */
		class WinMouseInput {
			friend class SFW::WindowHandler;

		private:
			/**
			 * @brief コンストラクタ
			 * @param hwnd ウィンドウハンドル
			 */
			explicit WinMouseInput(HWND hwnd);
			/**
			 * @brief Raw Inputの登録と解除
			 * @param enable 有効化するかどうか
			 * @param noLegacy レガシーメッセージ(WM_MOUSEMOVEなど)を無効化するかどうか
			 * @param capture キャプチャモード(有効中は他のウィンドウを触れない)にするかどうか
			 */
			void RegisterRawInput(bool enable, bool noLegacy = true, bool capture = true);
			/**
			 * @brief Raw Inputメッセージの処理
			 * @param lParam メッセージのパラメータ
			 */
			void HandleRawInput(LPARAM lParam);
			/**
			 * @brief ウィンドウのフォーカス取得の処理
			 */
			void OnFocus();
			/**
			 * @brief ウィンドウのフォーカス喪失の処理
			 */
			void OnFocusLost();
			/**
			 * @brief クリッピングの再設定
			 */
			void Reclip();
			/**
			 * @brief 蓄積されたマウスの移動量を取得し、リセットする
			 * @param outDx マウスのX方向の移動量
			 * @param outDy マウスのY方向の移動量
			 */
			void ConsumeDelta(LONG& outDx, LONG& outDy) noexcept;
			/**
			 * @brief デバイスのクリーンアップ
			 */
			void Cleanup();
		public:
			/**
			 * @brief マウスキャプチャの切り替え
			 * @param on キャプチャを有効にするかどうか
			 */
			void ToggleCapture(bool on);
			/**
			 * @brief 蓄積されたマウスの移動量を取得する
			 * @param outDx マウスのX方向の移動量
			 * @param outDy マウスのY方向の移動量
			 */
			void GetDelta(LONG& outDx, LONG& outDy) const noexcept { outDx = dx; outDy = dy; }
			/**
			 * @brief マウスホイールの回転量を取得する
			 * @param outWheelV 垂直ホイールの回転量
			 * @param outWheelH 水平ホイールの回転量
			 */
			void GetMouseWheel(int& outWheelV, int& outWheelH) const noexcept { outWheelV = wheelV; outWheelH = wheelH; }
			/**
			 * @brief マウスの状態を取得する関数
			 * @return bool マウスがキャプチャされているかどうか
			 */
			bool IsCaptured() const noexcept { return captured; }
			/**
			 * @brief 左ボタンが押されているかどうかを取得する関数
			 * @return bool 左ボタンが押されている場合はtrue、そうでない場合はfalse
			 */
			bool IsLeftDown() const noexcept { return lDown; }
			/**
			 * @brief 右ボタンが押されているかどうかを取得する関数
			 * @return bool 右ボタンが押されている場合はtrue、そうでない場合はfalse
			 */
			bool IsRightDown() const noexcept { return rDown; }

		private:
			HWND hwnd;
			bool captured = false;
			bool lDown = false;
			bool rDown = false;
			bool mDown = false;
			LONG dx = 0;
			LONG dy = 0;
			int wheelV = 0; // 垂直ホイールの回転量（1 = 一段）
			int wheelH = 0; // 水平ホイールの回転量（1 = 一段）

			std::vector<BYTE> rawBuffer;
		};
	}
}
