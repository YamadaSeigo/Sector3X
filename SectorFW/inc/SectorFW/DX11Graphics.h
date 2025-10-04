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
			~DX11GraphicsDevice();

			DX11GraphicsDevice(DX11GraphicsDevice&& rhs) noexcept;
			DX11GraphicsDevice& operator=(DX11GraphicsDevice&& rhs) noexcept;
			DX11GraphicsDevice(const DX11GraphicsDevice&) = delete;
			DX11GraphicsDevice& operator=(const DX11GraphicsDevice&) = delete;
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
			 * @details RenderGraphを使用して描画を実行します。
			 */
			void DrawImpl();
			/**
			 * @brief 描画を実行する関数
			 * @details RenderGraphを使用して描画を実行します。
			 * @param renderGraph RenderGraphの参照
			 */
			void PresentImpl();

			void StartRenderThread();
			void StopRenderThread();
			void SubmitFrameImpl(const FLOAT clearColor[4], uint64_t frameIdx);
			void WaitSubmittedFramesImpl(uint64_t uptoFrame);

			RenderService* GetRenderService() noexcept {
				return renderGraph->GetRenderService();
			}

			void TestInitialize();

			ID3D11Device* GetDevice() const noexcept { return m_device.Get(); }
			ID3D11DeviceContext* GetDeviceContext() const noexcept { return m_context.Get(); }
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

				std::atomic<DX11GraphicsDevice*> owner{ nullptr };

				// フレーム進捗
				std::atomic<uint64_t> lastSubmitted{ 0 };
				std::atomic<uint64_t> lastCompleted{ 0 };

				// 上限（= バッファ数）
				static constexpr uint32_t MaxInFlight = RENDER_BUFFER_COUNT;
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

			std::unique_ptr<DX11MeshManager> meshManager;
			std::unique_ptr<DX11ShaderManager> shaderManager;
			std::unique_ptr<DX11TextureManager> textureManager;
			std::unique_ptr<DX11BufferManager> bufferManager;
			std::unique_ptr<DX11SamplerManager> samplerManager;
			std::unique_ptr<DX11MaterialManager> materialManager;
			std::unique_ptr<DX11PSOManager> psoManager;
			std::unique_ptr<DX11ModelAssetManager> modelAssetManager;

			std::unique_ptr<DX11Backend> backend;
			std::unique_ptr<DX11RenderGraph> renderGraph;

			std::shared_ptr<RTState> m_rt;

#ifdef _ENABLE_IMGUI
			Debug::GpuTimerD3D11 m_gpuTimer;
			double m_gpuTimeBudget = 0.f;
#endif // _ENABLE_IMGUI
		};
	}
}
