// ImGuiLayer.cpp
#include <map>

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
				ImGui::Begin("DebugVars");

				// 1. UIバッファに drain
				for (auto& c : bus.debugControlRegisterQ.drain()) {
					bus.debugControls.push_back(std::move(c));
				}

				// 2. カテゴリごとに一覧を作成
				std::map<std::string, std::vector<DebugControl*>> groups;
				for (auto& c : bus.debugControls) {
					groups[c.category].push_back(&c);
				}

				// 3. Tree 構造として描く
				for (auto& g : groups)
				{
					const std::string& category = g.first;
					auto& items = g.second;

					// ▼ or ▶ を描く
					if (ImGui::TreeNode(category.c_str()))
					{
						for (DebugControl* ctrl : items)
						{
							switch (ctrl->kind)
							{
							case DebugControlKind::DC_SLIDERFLOAT:
							{
								float v = ctrl->f_value;
								if (ImGui::DragFloat(ctrl->label.c_str(), &v, ctrl->f_speed, ctrl->f_min, ctrl->f_max))
								{
									ctrl->f_value = v;
									if (ctrl->onChangeF) ctrl->onChangeF(v);
								}
								break;
							}
							case DebugControlKind::DC_SLIDERINT:
							{
								int v = ctrl->i_value;
								if (ImGui::DragInt(ctrl->label.c_str(), &v, ctrl->f_speed, ctrl->i_min, ctrl->i_max))
								{
									ctrl->i_value = v;
									if (ctrl->onChangeI) ctrl->onChangeI(v);
								}
								break;
							}
							case DebugControlKind::DC_CHECKBOX:
							{
								bool v = ctrl->b_value;
								if (ImGui::Checkbox(ctrl->label.c_str(), &v))
								{
									ctrl->b_value = v;
									if (ctrl->onChangeB) ctrl->onChangeB(v);
								}
								break;
							}
							}
						}

						ImGui::TreePop();
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