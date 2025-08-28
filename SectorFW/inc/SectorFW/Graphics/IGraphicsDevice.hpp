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
			bool Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height) {
				return static_cast<Impl*>(this)->InitializeImpl(nativeWindowHandle, width, height);
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
			void Configure(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height)
			{
				assert(!m_isInitialized && "IGraphicsDevice is already initialized.");

				m_isInitialized = Initialize(nativeWindowHandle, width, height);
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
		};
	}
}