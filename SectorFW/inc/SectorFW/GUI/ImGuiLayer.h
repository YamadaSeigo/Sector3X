// ImGuiLayer.h
#pragma once

#if _DEBUG
 // デバッグビルド時は ImGui を有効化
#define _ENABLE_IMGUI
#endif // _DEBUG

#include <memory>

#include "ImGuiBackend.h"

namespace SectorFW
{
    namespace GUI {

        class IImGuiBackend;

        class ImGuiLayer {
        public:
            //UIBus.cppで実装
            explicit ImGuiLayer(std::unique_ptr<IImGuiBackend> backend);
            ~ImGuiLayer();

			const std::type_info& GetWindowType() const {
				return backend_->GetWindowType();
			}
			const std::type_info& GetDeviceType() const {
				return backend_->GetDeviceType();
			}
            bool Init(const ImGuiInitInfo& info);
            void BeginFrame();      // ImGui::NewFrame() を含む
			void DrawUI();          // UIBus.cppで実装
            void EndFrame();        // ImGui::Render() まで
            void Render();          // backend->Render()

        private:
            std::unique_ptr<IImGuiBackend> backend_;
        };
    }
}

