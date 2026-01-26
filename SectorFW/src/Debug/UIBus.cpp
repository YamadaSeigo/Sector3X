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

		void PublishLogicMs(float ms) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().logicMs.publish(ms);
		}
		void PublishRenderMs(float ms) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().renderMs.publish(ms);
		}
		void PublishGpuFrameMs(float ms) {
			if (GetUIBus().alive.load(std::memory_order_acquire)) GetUIBus().gpuFrameMs.publish(ms);
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
			std::function<void(float)> onChange,
			float* bound)
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

			c.f_target = bound;

			bus.debugControlRegisterQ.push(std::move(c));
		}

		void RegisterDebugCheckBox(const std::string& category, const std::string& label, bool initialValue, std::function<void(bool)> onChange, bool* bound)
		{
			auto& bus = GetUIBus();
			if (!bus.alive.load(std::memory_order_acquire)) return;

			DebugControl c;
			c.kind = DebugControlKind::DC_CHECKBOX;
			c.category = category;
			c.label = label;
			c.b_value = initialValue;
			c.onChangeB = std::move(onChange);

			c.b_target = bound;

			bus.debugControlRegisterQ.push(std::move(c));
		}

		void RegisterDebugButton(
			const std::string& category,
			const std::string& label,
			std::function<void(bool)> onChange)
		{
			auto& bus = GetUIBus();
			if (!bus.alive.load(std::memory_order_acquire)) return;

			DebugControl c;
			c.kind = DebugControlKind::DC_BUTTON;
			c.category = category;
			c.label = label;
			c.onChangeB = std::move(onChange);

			bus.debugControlRegisterQ.push(std::move(c));
		}

		void RegisterDebugText(
			const std::string& category,
			const std::string& label,
			const std::string& initial,
			std::function<void(std::string)> onChange)
		{
			auto& bus = GetUIBus();
			if (!bus.alive.load(std::memory_order_acquire)) return;

			DebugControl c;
			c.kind = DebugControlKind::DC_STRING;
			c.category = category;
			c.label = label;
			c.onChangeText = std::move(onChange);

			// 初期文字列をバッファにコピー
			std::snprintf(c.textBuf, DebugControl::TextBufSize, "%s", initial.c_str());

			bus.debugControlRegisterQ.push(std::move(c));
		}

	}

#ifdef _ENABLE_IMGUI
#define PUSH_IMGUI_LOG(s) void PushLog(std::string s)
#else
#define PUSH_IMGUI_LOG(s)
#endif
}