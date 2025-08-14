#pragma once
#include <windows.h>
#include <hidusage.h>
#include <vector>

namespace SectorFW
{
	class WindowHandler;

	namespace Input
	{
		class WinMouseInput {
			friend class SectorFW::WindowHandler;

		private:
			explicit WinMouseInput(HWND hwnd);
			void RegisterRawInput(bool enable, bool noLegacy = true, bool capture = true);
			void HandleRawInput(LPARAM lParam);

			void OnFocus();
			void OnFocusLost();
			void Reclip();

			void ConsumeDelta(LONG& outDx, LONG& outDy) noexcept;
		public:
			void ToggleCapture(bool on);
			void GetDelta(LONG& outDx, LONG& outDy) const noexcept { outDx = dx; outDy = dy; }
			void GetMouseWheel(int& outWheelV, int& outWheelH) const noexcept { outWheelV = wheelV; outWheelH = wheelH; }
			bool IsCaptured() const noexcept { return captured; }
			bool IsLeftDown() const noexcept { return lDown; }
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
