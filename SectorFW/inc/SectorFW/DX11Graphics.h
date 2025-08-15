/*****************************************************************//**
 * @file   DX11Graphics.h
 * @brief DX11用のグラフィックスデバイスクラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include "Graphics/IGraphicsDevice.hpp"
#include "Graphics/RenderGraph.hpp"
#include "Graphics/DX11/DX11RenderBackend.h"
#include "Graphics/DX11/DX113DCameraService.h"

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DX11用グラフィックスデバイスクラス
		 * @class DX11GraphicsDevice
		 */
		class DX11GraphicsDevice : public IGraphicsDevice<DX11GraphicsDevice>
		{
		public:
			using DX11RenderGraph = RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*>;

			/**
			 * @brief コンストラクタ
			 */
			DX11GraphicsDevice() = default;
			/**
			 * @brief デストラクタ
			 */
			~DX11GraphicsDevice() = default;
			/**
			 * @brief 初期化関数
			 * @param nativeWindowHandle ウィンドウハンドル
			 * @param width ウィンドウ幅
			 * @param height ウィンドウ高さ
			 * @return bool 初期化に成功したかどうか
			 */
			bool InitializeImpl(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height);
			/**
			 * @brief 画面をクリアする関数
			 * @param clearColor クリアカラー（RGBA）
			 */
			void ClearImpl(const FLOAT clearColor[4]);
			/**
			 * @brief 描画を実行する関数
			 * @details RenderGraphを使用して描画を実行します。
			 */
			void DrawImpl();
			/**
			 * @brief 描画を実行する関数
			 * @details RenderGraphを使用して描画を実行します。
			 * @param renderGraph RenderGraphの参照
			 */
			void PresentImpl();

			RenderService* GetRenderService() noexcept {
				return GetRenderGraph().GetRenderService();
			}

			void TestInitialize();
		private:
			/**
			 * @brief RenderGraphを取得する関数
			 * @detail コンパイルの遅延判定のために関数に梱包している。Deviceを初期化してから初期化させたいため
			 * @return DX11RenderGraph& RenderGraphの参照
			 */
			inline DX11RenderGraph& GetRenderGraph();
		private:
			// デバイス
			ComPtr<ID3D11Device> m_device;
			// デバイスコンテキスト
			ComPtr<ID3D11DeviceContext> m_context;
			// スワップチェーン
			ComPtr<IDXGISwapChain> m_swapChain;
			// デフォルトのレンダリングターゲットビュー
			ComPtr<ID3D11RenderTargetView> m_renderTargetView;
			// デフォルトの深度ステンシルバッファ
			ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
			// デフォルトの深度ステンシルビュー
			ComPtr<ID3D11DepthStencilView> m_depthStencilView;
		};
	}
}
