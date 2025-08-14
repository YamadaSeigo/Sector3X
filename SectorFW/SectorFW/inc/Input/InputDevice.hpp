#pragma once

#include "Core/ECS/ServiceContext.hpp"

namespace SectorFW
{
	namespace Input
	{
		// --- Key Enum ---
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

		template<typename Derived>
		class InputDevice : public ECS::IUpdateService
		{
			friend class ServiceLocator;

		public:
			bool IsKeyPressed(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyPressedImpl(key);
			}

			bool IsKeyReleased(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyReleasedImpl(key);
			}

			bool IsKeyTrigger(Key key) const {
				return static_cast<const Derived*>(this)->IsKeyTriggerImpl(key);
			}

			bool IsLButtonPressed() const {
				return static_cast<const Derived*>(this)->IsLButtonPressedImpl();
			}

			bool IsRButtonPressed() const {
				return static_cast<const Derived*>(this)->IsRButtonPressedImpl();
			}

			bool IsMouseCaptured() const {
				return static_cast<const Derived*>(this)->IsMouseCapturedImpl();
			}

			void SetMouseCaptured(bool captured) {
				static_cast<Derived*>(this)->SetMouseCapturedImpl(captured);
			}

			void GetMouseDelta(long& outDx, long& outDy) const noexcept {
				static_cast<const Derived*>(this)->GetMouseDeltaImpl(outDx, outDy);
			}

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