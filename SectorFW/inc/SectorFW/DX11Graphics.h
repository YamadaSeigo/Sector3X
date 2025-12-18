/*****************************************************************//**
 * @file   DX11Graphics.h
 * @brief DX11用のグラフィックスデバイスクラスを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include "Graphics/IGraphicsDevice.hpp"
#include "Graphics/RenderGraph.hpp"
#include "Graphics/RenderTypes.h"

#include "Graphics/DX11/DX11RenderBackend.h"
#include "Graphics/DX11/DX113DCameraService.h"
#include "Graphics/DX11/DX112DCameraService.h"

#ifdef _ENABLE_IMGUI
#include "Debug/GPUTimerD3D11.h"
#endif // _ENABLE_IMGUI

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace SFW
{
	namespace Graphics::DX11
	{
		/**
		 * @brief DX11用グラフィックスデバイスクラス
		 * @class DX11GraphicsDevice
		 */
		class GraphicsDevice : public IGraphicsDevice<GraphicsDevice>
		{
		public:
			using RenderGraph = RenderGraph<RenderBackend, ID3D11RenderTargetView*, ID3D11DepthStencilView*, ID3D11ShaderResourceView*, ID3D11Buffer*, ComPtr>;

			/**
			 * @brief コンストラクタ
			 */
			GraphicsDevice() = default;
			/**
			 * @brief デストラクタ
			 */
			~GraphicsDevice();

			GraphicsDevice(GraphicsDevice&& rhs) noexcept;
			GraphicsDevice& operator=(GraphicsDevice&& rhs) noexcept;
			GraphicsDevice(const GraphicsDevice&) = delete;
			GraphicsDevice& operator=(const GraphicsDevice&) = delete;
			/**
			 * @brief 初期化関数
			 * @param nativeWindowHandle ウィンドウハンドル
			 * @param width ウィンドウ幅
			 * @param height ウィンドウ高さ
			 * @return bool 初期化に成功したかどうか
			 */
			bool InitializeImpl(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height, double fps);
			/**
			 * @brief 画面をクリアする関数
			 * @param clearColor クリアカラー（RGBA）
			 */
			void ClearImpl(const FLOAT clearColor[4]);
			/**
			 * @brief 描画を実行する関数
			 * @detailss RenderGraphを使用して描画を実行します。
			 */
			void DrawImpl();
			/**
			 * @brief 描画を実行する関数
			 * @detailss RenderGraphを使用して描画を実行します。
			 * @param renderGraph RenderGraphの参照
			 */
			void PresentImpl();

			void StartRenderThread();
			void StopRenderThread();
			void SubmitFrameImpl(const FLOAT clearColor[4], uint64_t frameIdx);
			void WaitSubmittedFramesImpl(uint64_t uptoFrame);

			void SetDefaultRenderTarget();

			void SetBlendState(BlendStateID state);
			void SetDepthStencilState(DepthStencilStateID state, UINT stencilRef = 0);
			void SetRasterizerState(RasterizerStateID state);

			RenderService* GetRenderService() noexcept {
				return renderGraph->GetRenderService();
			}
			/**
			 * @brief 指定した関数オブジェクトに引数を渡して実行する
			 * @param func RenderGraph,ID3D11RenderTargetView,ID3D11DepthStencilViewを引数に取る関数オブジェクト
			 */
			template<typename F>
			void ExecuteCustomFunc(F&& func)
			{
				func(renderGraph.get(), m_renderTargetView, m_depthStencilView);
			}

			ID3D11Device* GetDevice() const noexcept { return m_device.Get(); }
			ID3D11DeviceContext* GetDeviceContext() const noexcept { return m_context.Get(); }

			const RenderBackend* const GetBackend() const noexcept {
				return backend.get();
			}

			ComPtr<ID3D11RenderTargetView> GetMainRenderTargetView() const noexcept {
				return m_renderTargetView;
			}
			ComPtr<ID3D11DepthStencilView> GetMainDepthStencilView() const noexcept {
				return m_depthStencilView;
			}

			const D3D11_VIEWPORT& GetMainViewport() const noexcept {
				return m_viewport;
			}

		private:
			// ===== レンダースレッド実装 =====
			struct RenderSubmit {
				FLOAT clearColor[4]{ 0,0,0,1 };
				uint64_t frameIdx{};
				bool doClear{ true };
			};

			struct RTState {
				std::mutex qMtx;
				std::condition_variable qCv;
				std::deque<RenderSubmit> queue;

				std::mutex doneMtx;
				std::condition_variable doneCv;

				std::atomic<bool> running{ false };
				std::thread thread;

				std::atomic<GraphicsDevice*> owner{ nullptr };

				// フレーム進捗
				std::atomic<uint64_t> lastSubmitted{ 0 };
				std::atomic<uint64_t> lastCompleted{ 0 };

				// 上限（= バッファ数）
				static constexpr uint16_t MaxInFlight = RENDER_BUFFER_COUNT;
			};

			void RenderThreadMain(std::shared_ptr<RTState> st);
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
			// デフォルトの深度のSRV
			ComPtr<ID3D11ShaderResourceView> m_depthStencilSRV;

			D3D11_VIEWPORT m_viewport{};

			std::unique_ptr<MeshManager> meshManager;
			std::unique_ptr<ShaderManager> shaderManager;
			std::unique_ptr<TextureManager> textureManager;
			std::unique_ptr<BufferManager> bufferManager;
			std::unique_ptr<SamplerManager> samplerManager;
			std::unique_ptr<MaterialManager> materialManager;
			std::unique_ptr<PSOManager> psoManager;
			std::unique_ptr<ModelAssetManager> modelAssetManager;

			std::unique_ptr<RenderBackend> backend;
			std::unique_ptr<RenderGraph> renderGraph;

			std::shared_ptr<RTState> m_rt;

#ifdef _ENABLE_IMGUI
			Debug::GpuTimerD3D11 m_gpuTimer;
			double m_gpuTimeBudget = 0.f;
#endif // _ENABLE_IMGUI
		};
	}
}
