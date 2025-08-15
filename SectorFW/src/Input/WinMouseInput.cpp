#include "Input/WinMouseInput.h"

namespace SectorFW
{
	namespace Input
	{
		WinMouseInput::WinMouseInput(HWND hwnd) : hwnd(hwnd) {}

		void WinMouseInput::RegisterRawInput(bool enable, bool noLegacy, bool capture) {
			RAWINPUTDEVICE rid{};
			rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
			rid.usUsage = HID_USAGE_GENERIC_MOUSE;
			rid.dwFlags = noLegacy ? RIDEV_NOLEGACY : 0;
			rid.hwndTarget = hwnd;
			if (enable) {
				rid.dwFlags = 0;
				if (noLegacy)  rid.dwFlags |= RIDEV_NOLEGACY;      // WM_MOUSEMOVE等を抑止
				if (captured)   rid.dwFlags |= RIDEV_CAPTUREMOUSE;  // ウィンドウ外でも相対入力
				// 必要なら RIDEV_INPUTSINK で非フォーカス時も受け取れる
			}
			else {
				rid.dwFlags = RIDEV_REMOVE;
				rid.hwndTarget = nullptr;
			}

			RegisterRawInputDevices(&rid, 1, sizeof(rid));
		}

		void WinMouseInput::HandleRawInput(LPARAM lParam) {
			UINT size = 0;
			GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
			if (!size) return;

			if (rawBuffer.size() < size) rawBuffer.resize(size);
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawBuffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;

			auto* raw = reinterpret_cast<RAWINPUT*>(rawBuffer.data());
			if (raw->header.dwType != RIM_TYPEMOUSE) return;

			const RAWMOUSE& m = raw->data.mouse;
			if (m.usFlags == MOUSE_MOVE_RELATIVE) {
				dx += m.lLastX;
				dy += m.lLastY;
			}
			if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)  lDown = true;
			if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)    lDown = false;
			if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) rDown = true;
			if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)   rDown = false;
			if (m.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) mDown = true;
			if (m.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)   mDown = false;

			// ホイール
			if (m.usButtonFlags & RI_MOUSE_WHEEL) {
				SHORT wheelDelta = (SHORT)m.usButtonData; // 垂直
				wheelV += wheelDelta / WHEEL_DELTA;       // 120単位 → ステップ数
			}
			if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
				SHORT wheelDelta = (SHORT)m.usButtonData; // 水平
				wheelH += wheelDelta / WHEEL_DELTA;
			}
		}

		void WinMouseInput::OnFocus() {
			ToggleCapture(true);
		}

		void WinMouseInput::OnFocusLost() {
			ToggleCapture(false);
		}

		void WinMouseInput::Reclip() {
			if (!captured) return;
			RECT rc; GetClientRect(hwnd, &rc);
			POINT lt{ rc.left, rc.top }, rb{ rc.right, rc.bottom };
			ClientToScreen(hwnd, &lt);
			ClientToScreen(hwnd, &rb);
			RECT clip{ lt.x, lt.y, rb.x, rb.y };
			ClipCursor(&clip);
		}

		void WinMouseInput::ConsumeDelta(LONG& outDx, LONG& outDy) noexcept {
			outDx = dx;
			outDy = dy;
			dx = 0; dy = 0;
			wheelH = 0; wheelV = 0;
		}

		void WinMouseInput::ToggleCapture(bool on) {
			if (captured == on) return;
			captured = on;

			if (captured) {
				while (ShowCursor(FALSE) >= 0) {}
				Reclip();
				RegisterRawInput(true, /*noLegacy=*/true, /*capture=*/true);
				SetCapture(hwnd);
				SetFocus(hwnd);
			}
			else {
				RegisterRawInput(false);
				ClipCursor(nullptr);
				while (ShowCursor(TRUE) < 0) {}
				ReleaseCapture();
			}
		}
	}
}