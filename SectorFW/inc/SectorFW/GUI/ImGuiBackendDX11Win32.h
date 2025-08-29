// ImGuiBackend_DX11_Win32.cpp
#include "ImGuiBackend.h"
#include "../external/imgui/imgui.h"
#include "../external/imgui/imgui_impl_win32.h"
#include "../external/imgui/imgui_impl_dx11.h"

namespace SectorFW
{
    namespace GUI
    {
        class ImGuiBackendDX11Win32 : public IImGuiBackend {
        public:
			const std::type_info& GetWindowType() const override {
				return typeid(HWND);
			}
			const std::type_info& GetDeviceType() const override {
				return typeid(ID3D11Device*);
			}

            bool Init(const ImGuiInitInfo& info) override {
                if (!ImGui_ImplWin32_Init(info.platform_window)) return false;
                if (!ImGui_ImplDX11_Init((ID3D11Device*)info.device,
                    (ID3D11DeviceContext*)info.device_context)) return false;
                return true;
            }
            void NewFrame() override {
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
            }
            void Render() override {
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                // Viewports —LŒø‚È‚ç
                auto& io = ImGui::GetIO();
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
            }
            void Shutdown() override {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
            }
        };
    }
}

