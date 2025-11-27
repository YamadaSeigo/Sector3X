// ImGuiLayer.cpp
#include "Debug/ImGuiLayer.h"
#include "Debug/UIBus.h"
#include "Debug/PerfHUD.h"
#include "imgui/imgui.h"

#include "WindowHandler.h"

namespace SFW
{
	namespace Debug
	{
		static PerfHUD gPerfHUD;

		ImGuiLayer::ImGuiLayer(std::unique_ptr<IImGuiBackend> backend)
			: backend_(std::move(backend)) {
			StartUIBus();
			gPerfHUD.Init();
		}

		ImGuiLayer::~ImGuiLayer() {
			if (backend_) backend_->Shutdown();
			ImGui::DestroyContext();
			StopUIBus();
		}

		bool ImGuiLayer::Init(const ImGuiInitInfo& info) {
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();

			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
			//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 任意（各バックエンドの対応が必要）

			ImGui::StyleColorsDark();
			return backend_->Init(info);
		}

		void ImGuiLayer::BeginFrame() {
			backend_->NewFrame();
			ImGui::NewFrame();
		}

		void ImGuiLayer::DrawUI(float frameSec) {
			auto& bus = GetUIBus();

			ImGui::Begin("Perf");
			gPerfHUD.TickAndDraw(frameSec, false);
			ImGui::End();

			static std::vector<std::string> logBuf;
			for (auto& s : bus.logQ.drain()) logBuf.push_back(std::move(s));
			ImGui::Begin("Log"); for (auto& s : logBuf) ImGui::TextUnformatted(s.c_str()); ImGui::End();

			// --- ツリースナップショットを反映 ---
			bus.tree.swap(); // back<->front
			const auto& tree = bus.tree.read();

			ImGui::Begin("Tree");
			int openDepth = 0;
			for (size_t i = 0; i < tree.items.size(); ++i)
			{
				const auto& it = tree.items[i];

				// 1) いま描くノードより深く“開いている”階層は閉じる
				while (openDepth > (int)it.depth) { ImGui::TreePop(); --openDepth; }

				// 2) 親が閉じているサブツリーはスキップ（pre-order を想定）
				if ((int)it.depth > openDepth) {
					// 親が閉じている＝このノードは表示しない
					continue;
				}

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
				if (it.leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

				// opened==true の時だけ内部で Push される（＝後で TreePop が必要）
				const bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)it.id, flags, "%s", it.label.c_str());

				// Leaf+NoTreePushOnOpen は opened でも Push されないので、openDepth を増やさない
				if (!it.leaf && opened) {
					++openDepth; // “実際に Push された”ときだけカウント
				}
			}
			// 3) 残っている分だけ閉じる（Push された数だけ）
			while (openDepth > 0) { ImGui::TreePop(); --openDepth; }
			ImGui::End();

			const auto& t = bus.snap.read();
			ImGui::Begin("Telemetry");
			ImGui::Text("cpu=%.2f gpu=%.2f", t.cpu, t.gpu);
			ImGui::End();

			ImGuiIO& io = ImGui::GetIO();
			ImGui::Begin("MouseDebug");
			ImGui::Text("MousePos: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
			ImGui::Text("DisplaySize: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
			ImGui::Text("FramebufferScale: %.2f, %.2f",
				io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

			// ================================
		   // DebugVars ウィンドウ
		   // ================================
			{
				// まず登録キューから UI 側バッファに取り込み
				for (auto& c : bus.debugControlRegisterQ.drain()) {
					bus.debugControls.push_back(std::move(c));
				}

				ImGui::Begin("DebugVars");
				for (auto& c : bus.debugControls)
				{
					switch (c.kind)
					{
					case DebugControlKind::DC_SLIDERFLOAT:
					{
						float v = c.f_value;
						if (ImGui::DragFloat(c.label.c_str(), &v, c.f_speed, c.f_min, c.f_max))
						{
							c.f_value = v;
							if (c.onChangeF) c.onChangeF(v); // 指定されたコールバックに代入させる
						}
						break;
					}
					case DebugControlKind::DC_SLIDERINT:
					{
						int v = c.i_value;
						if (ImGui::DragInt(c.label.c_str(), &v, c.f_speed, c.i_min, c.i_max))
						{
							c.i_value = v;
							if (c.onChangeI) c.onChangeI(v);
						}
						break;
					}
					case DebugControlKind::DC_CHECKBOX:
					{
						bool v = c.b_value;
						if (ImGui::Checkbox(c.label.c_str(), &v))
						{
							c.b_value = v;
							if (c.onChangeB) c.onChangeB(v);
						}
						break;
					}
					default: break;
					}
				}
				ImGui::End();
			}

			ImGui::End();
		}

		void ImGuiLayer::EndFrame() {
			ImGui::EndFrame();
			ImGui::Render();
		}
		void ImGuiLayer::Render() {
			backend_->Render();
		}
	}
}