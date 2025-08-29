#include "Input/WinMouseInput.h"

#include "Graphics/IGraphicsDevice.hpp"

#ifdef _ENABLE_IMGUI
#include "../external/imgui/imgui.h"
#endif // _D3D11_IMGUI

namespace SectorFW
{
	namespace Input
	{
		WinMouseInput::WinMouseInput(HWND hwnd) : hwnd(hwnd) {}

		void WinMouseInput::RegisterRawInput(bool enable, bool noLegacy, bool capture) {
			RAWINPUTDEVICE rid{};
			rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
			rid.usUsage = HID_USAGE_GENERIC_MOUSE;
			rid.hwndTarget = hwnd;
			rid.dwFlags = 0;

			if (!enable) {
				rid.dwFlags = RIDEV_REMOVE;
				rid.hwndTarget = nullptr;
			}
			else {
				if (noLegacy) rid.dwFlags |= RIDEV_NOLEGACY;     // WM_* を止める
				if (capture)  rid.dwFlags |= RIDEV_CAPTUREMOUSE; // フォーカス外でも WM_INPUT
				// 必要なら rid.dwFlags |= RIDEV_INPUTSINK;
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
			if (captured) ToggleCapture(false);

			// レガシー系WM_*を復活させる or RawInputを通常モードで再登録
			RegisterRawInput(true, /*noLegacy=*/false, /*capture=*/false);

			ClipCursor(nullptr);
			while (ShowCursor(TRUE) < 0) {}
			ReleaseCapture();

#ifdef _ENABLE_IMGUI
			// --- ImGui のマウス状態をリセット ---
			ImGuiIO& io = ImGui::GetIO();
			io.AddMouseButtonEvent(0, false);
			io.AddMouseButtonEvent(1, false);
			io.AddMouseButtonEvent(2, false);
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
#if IMGUI_VERSION_NUM >= 18900
			io.AddFocusEvent(false);
#endif // IMGUI_VERSION_NUM
#endif // _ENABLE_IMGUI
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
				RegisterRawInput(true, /*noLegacy=*/true,  /*capture=*/true); // レガシー停止
				SetCapture(hwnd);
				SetFocus(hwnd);
			}
			else {
#ifdef _ENABLE_IMGUI
				// ---- ImGui の押下状態が残っている可能性をクリア ----
				ImGuiIO& io = ImGui::GetIO();
				io.AddMouseButtonEvent(0, false);
				io.AddMouseButtonEvent(1, false);
				io.AddMouseButtonEvent(2, false);
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); // “マウス不在”を通知（任意）
#endif // _ENABLE_IMGUI

				// ---- レガシー復活（ImGui が WM_* を再び受け取れる）----
				RegisterRawInput(true, /*noLegacy=*/false, /*capture=*/false);
				// あるいは RegisterRawInput(false); でも良いが、明示再登録がより確実

				ClipCursor(nullptr);
				while (ShowCursor(TRUE) < 0) {}
				ReleaseCapture();
			}
		}
	}
}