#include "Debug/UIBus.h"
#include "Debug/ImGuiLayer.h"
#include "Debug/PerfHUD.h"

#ifdef _ENABLE_IMGUI
#include "../external/imgui/imgui.h"
#endif	// _D3D11_IMGUI

namespace SFW
{
	namespace Debug
	{
		// --- グローバル取得（関数内 static ＝初期化順問題を回避）
		UIBus& GetUIBus() {
			static UIBus bus; // Meyers Singleton
			return bus;
		}

		void StartUIBus() { GetUIBus().alive.store(true, std::memory_order_release); }
		void StopUIBus() { GetUIBus().alive.store(false, std::memory_order_release); }

		void PublishCpu(float v) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().cpuLoad.publish(v);
		}

		void PublishGpu(float v) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().gpuLoad.publish(v);
		}
		void PublishStatus(std::string s) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().status.publish(std::move(s));
		}

		void PushLog(std::string s) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().logQ.push(std::move(s));
		}

		UiSnapshot::WriteGuard BeginTelemetryWrite() { return GetUIBus().snap.beginWrite(); }
		UiTreeSnapshot::WriteGuard BeginTreeWrite() { return GetUIBus().tree.beginWrite(); }

		// ================================
	    // デバッグコントロール登録実装
	    // ================================
		void RegisterDebugSliderFloat(
			const std::string& category,
			const std::string& label,
			float initialValue,
			float minValue,
			float maxValue,
			float speed,
			std::function<void(float)> onChange)
		{
			auto& bus = GetUIBus();
			if (!bus.alive.load(std::memory_order_acquire)) return;

			DebugControl c;
			c.kind = DebugControlKind::DC_SLIDERFLOAT;
			c.category = category;
			c.label = label;
			c.f_value = initialValue;
			c.f_min = minValue;
			c.f_max = maxValue;
			c.f_speed = speed;
			c.onChangeF = std::move(onChange);

			bus.debugControlRegisterQ.push(std::move(c));
		}
	}

#ifdef _ENABLE_IMGUI
#define PUSH_IMGUI_LOG(s) void PushLog(std::string s)
#else
#define PUSH_IMGUI_LOG(s)
#endif
}