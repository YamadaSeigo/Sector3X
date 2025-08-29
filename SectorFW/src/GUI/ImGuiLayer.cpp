// ImGuiLayer.cpp
#include "GUI/ImGuiLayer.h"
#include "imgui/imgui.h"

namespace SectorFW
{
    namespace GUI
    {
        bool ImGuiLayer::Init(const ImGuiInitInfo& info) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();

            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;      // 任意
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 任意（各バックエンドの対応が必要）

            ImGui::StyleColorsDark();
            return backend_->Init(info);
        }

        void ImGuiLayer::BeginFrame() {
            backend_->NewFrame();
            ImGui::NewFrame();
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