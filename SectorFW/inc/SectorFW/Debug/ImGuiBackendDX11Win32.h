/*****************************************************************//**
 * @file   ImGuiBackendDX11Win32.h
 * @brief ImGuiのDirectX11とWin32のバックエンドを実装するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#include "ImGuiBackend.h"
#include "../external/imgui/imgui.h"
#include "../external/imgui/imgui_impl_win32.h"
#include "../external/imgui/imgui_impl_dx11.h"

namespace SectorFW
{
	namespace Debug
	{
		/**
		 * @brief ImGuiのDirectX11とWin32のバックエンドを実装するクラス
		 */
		class ImGuiBackendDX11Win32 : public IImGuiBackend {
		public:
			/**
			 * @brief ウィンドウの型情報を取得します。
			 * @return const std::type_info& ウィンドウの型情報
			 */
			const std::type_info& GetWindowType() const override {
				return typeid(HWND);
			}
			/**
			 * @brief デバイスの型情報を取得します。
			 * @return const std::type_info& デバイスの型情報
			 */
			const std::type_info& GetDeviceType() const override {
				return typeid(ID3D11Device*);
			}
			/**
			 * @brief デバイスコンテキストの型情報を取得します。
			 * @param info 初期化情報
			 * @return 成功ならtrue、失敗ならfalse
			 */
			bool Init(const ImGuiInitInfo& info) override {
				if (!ImGui_ImplWin32_Init(info.platform_window)) return false;
				if (!ImGui_ImplDX11_Init((ID3D11Device*)info.device,
					(ID3D11DeviceContext*)info.device_context)) return false;
				return true;
			}
			/**
			 * @brief 新しいフレームを開始します。
			 */
			void NewFrame() override {
				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
			}
			/**
			 * @brief 描画コマンドを実行します。
			 */
			void Render() override {
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				// Viewports 有効なら
				auto& io = ImGui::GetIO();
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
					ImGui::UpdatePlatformWindows();
					ImGui::RenderPlatformWindowsDefault();
				}
			}
			/**
			 * @brief バックエンドをシャットダウンします。
			 */
			void Shutdown() override {
				ImGui_ImplDX11_Shutdown();
				ImGui_ImplWin32_Shutdown();
			}
		};
	}
}
