#include "GUI/UIBus.h"
#include "GUI/ImGuiLayer.h"

#ifdef _ENABLE_IMGUI
#include "../external/imgui/imgui.h"
#endif	// _D3D11_IMGUI

namespace SectorFW
{
	namespace GUI
	{
		// --- グローバル取得（関数内 static ＝初期化順問題を回避）
		inline UIBus& GetUIBus() {
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


		// --- ImGuiLayer 実装
		//=========================================================================
		ImGuiLayer::ImGuiLayer(std::unique_ptr<IImGuiBackend> backend)
			: backend_(std::move(backend)) {
			StartUIBus();
		}

		ImGuiLayer::~ImGuiLayer() {
			if (backend_) backend_->Shutdown();
			ImGui::DestroyContext();
			StopUIBus();
		}

		void ImGuiLayer::DrawUI() {
			auto& bus = GetUIBus();
			ImGui::Begin("Perf");
			ImGui::Text("CPU: %.1f%%  GPU: %.1f%%",
				bus.cpuLoad.consume() * 100.f,
				bus.gpuLoad.consume() * 100.f);
			ImGui::Text("Status: %s", bus.status.consume().c_str());
			ImGui::End();

			static std::vector<std::string> logBuf;
			for (auto& s : bus.logQ.drain()) logBuf.push_back(std::move(s));
			ImGui::Begin("Log"); for (auto& s : logBuf) ImGui::TextUnformatted(s.c_str()); ImGui::End();

			const auto& t = bus.snap.read();
			ImGui::Begin("Telemetry");
			ImGui::Text("cpu=%.2f gpu=%.2f", t.cpu, t.gpu);
			ImGui::End();
		}
		//=========================================================================
	}
}
