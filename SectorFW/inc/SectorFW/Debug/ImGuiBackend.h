/*****************************************************************//**
 * @file   ImGuiBackend.h
 * @brief ImGuiのバックエンドインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <cstdint>

namespace SFW
{
	namespace Debug {
		/**
		 * @brief ImGuiの初期化情報を格納する構造体
		 */
		struct ImGuiInitInfo {
			void* platform_window = nullptr; // HWND / SDL_Window* / GLFWwindow* / NSWindow* など
			void* device = nullptr;          // ID3D11Device* / VkDevice / MTLDevice* など
			void* device_context = nullptr;  // ID3D11DeviceContext* / VkQueue / CAMetalLayer* など
			int   display_w = 0, display_h = 0;
			float dpi_scale = 1.0f;
		};
		/**
		 * @brief ImGuiのバックエンドインターフェース(抽象クラス)
		 */
		class IImGuiBackend {
		public:
			virtual ~IImGuiBackend() = default;
			virtual const std::type_info& GetWindowType() const = 0;
			virtual const std::type_info& GetDeviceType() const = 0;
			virtual bool Init(const ImGuiInitInfo&) = 0;
			virtual void NewFrame() = 0;
			virtual void Render() = 0;
			virtual void Shutdown() = 0;
			// （必要なら）リサイズ・フォント再生成・マルチビューポート対応など
			virtual void OnResize(int w, int h) {}
		};
		/**
		 * @brief IImGuiBackendを継承しているかをチェックするコンセプト
		 */
		template<typename T>
		concept ImGuiBackendType = std::is_base_of_v<IImGuiBackend, T>;
	}
}
