/*****************************************************************//**
 * @file   IGraphicsDevice.h
 * @brief  グラフィックデバイスのインターフェースクラスのまとめ
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#include <wrl/client.h>
#include <variant>
#include <string>
#include <memory>
#include <type_traits>

#include "../Debug/ImGuiLayer.h"

using namespace Microsoft::WRL;

namespace SectorFW
{
	namespace Graphics
	{
		// ウィンドウハンドルの型
		//======================================================================
		using NativeWindowHandle = std::variant<HWND>;
		//======================================================================

		/**
		 * @brief グラフィックデバイスのインターフェース
		 * @class IGraphicsDevice
		 */
		template<typename Impl>
		class IGraphicsDevice {
		protected:
			/**
			 * @brief 初期化
			 * @param nativeWindowHandle ウィンドウハンドル
			 * @param width ウィンドウ幅
			 * @param height ウィンドウ高さ
			 */
			bool Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height, double fps) {
				return static_cast<Impl*>(this)->InitializeImpl(nativeWindowHandle, width, height, fps);
			}
		public:
			/**
			 * @brief コンストラクタ
			 */
			IGraphicsDevice() = default;
			/**
			 * @brief 初期化
			 * @param nativeWindowHandle ウィンドウハンドル
			 * @param width ウィンドウ幅
			 * @param height ウィンドウ高さ
			 */
			template<Debug::ImGuiBackendType ImGuiBackend>
			void Configure(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height, double fps)
			{
				assert(!m_isInitialized && "IGraphicsDevice is already initialized.");

				m_isInitialized = Initialize(nativeWindowHandle, width, height, fps);

#ifdef _ENABLE_IMGUI
				m_imguiLayer = std::make_unique<Debug::ImGuiLayer>(std::make_unique<ImGuiBackend>());
				Debug::ImGuiInitInfo info{};
				void* windowPtr = std::visit([&](auto& handle)->void* {
					using T = std::decay_t<decltype(handle)>;
					if (typeid(T) != m_imguiLayer->GetWindowType()) {
						assert(false && "Incompatible window handle type for ImGui initialization.");
						return nullptr;
					}

					if constexpr (std::is_pointer_v<T>) {
						// 例: HWND / GLFWwindow* / SDL_Window* など
						return handle;                 // T* → void* へ暗黙変換（OK）
					}
					else {
						// 例: X11 Window のように整数ハンドルを持たせているならここは再検討
						return static_cast<void*>(std::addressof(handle)); // T* → void*（OK）
					}
					}, nativeWindowHandle);

				if (!windowPtr) {
					assert(false && "Failed to get window handle for ImGui initialization.");
					return;
				}
				info.platform_window = windowPtr;

				//info.platform_window = ;            // SDL3 なら SDL_Window*
				auto device = static_cast<Impl*>(this)->GetDevice();
				if (!device) {
					assert(false && "Failed to get graphics device for ImGui initialization.");
					return;
				}
				if (typeid(device) != m_imguiLayer->GetDeviceType()) {
					assert(false && "Incompatible graphics device type for ImGui initialization.");
					return;
				}

				info.device = device;
				auto deviceContext = static_cast<Impl*>(this)->GetDeviceContext();
				if (!deviceContext) {
					assert(false && "Failed to get graphics device context for ImGui initialization.");
					return;
				}
				info.device_context = deviceContext;
				info.display_w = (int)width;
				info.display_h = (int)height;
				info.dpi_scale = 1.0f; // 必要なら取得して入れる

				m_imguiLayer->Init(info);
				frameSec = 1.0f / (float)fps;
#endif // _D3D11_IMGUI
			}
			/**
			 * @brief 画面をクリア
			 * @param clearColor
			 */
			void Clear(const float clearColor[4]) {
				static_cast<Impl*>(this)->ClearImpl(clearColor);
			}
			void Draw() {
				static_cast<Impl*>(this)->DrawImpl();

#ifdef _ENABLE_IMGUI
				if (m_imguiLayer) {
					m_imguiLayer->BeginFrame();

					m_imguiLayer->DrawUI(frameSec);

					m_imguiLayer->EndFrame();
					m_imguiLayer->Render();
				}
#endif // _D3D11_IMGUI
			}
			/**
			 * @brief 描画
			 */
			void Present() {
				static_cast<Impl*>(this)->PresentImpl();
			}
			/**
			 * @brief フレームの提出
			 * @param clearColor クリアカラー
			* @param frameIdx フレームインデックス
			*/
			void SubmitFrame(const FLOAT clearColor[4], uint64_t frameIdx) {
				static_cast<Impl*>(this)->SubmitFrameImpl(clearColor, frameIdx);
			}
			/**
			 * @brief 提出済みフレームの完了待ち
			 * @param uptoFrame フレームインデックス
			 */
			void WaitSubmittedFrames(uint64_t uptoFrame) {
				static_cast<Impl*>(this)->WaitSubmittedFramesImpl(uptoFrame);
			}
			/**
			 * @brief デストラクタ
			 */
			~IGraphicsDevice() = default;
			/**
			 * @brief 初期化フラグの取得
			 * @return 初期化フラグ
			 */
			bool IsInitialized() const { return m_isInitialized; }
		private:
			// 初期化フラグ
			static inline bool m_isInitialized = false;

#ifdef _ENABLE_IMGUI
			std::unique_ptr<Debug::ImGuiLayer> m_imguiLayer = nullptr;
			float frameSec = 1.0f / 60.0f;
#endif // _D3D11_IMGUI
		};
	}
}