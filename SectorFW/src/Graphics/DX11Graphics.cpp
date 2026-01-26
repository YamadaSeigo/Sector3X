#include "DX11Graphics.h"

#include "Debug/logger.h"

#include "Math/convert.hpp"

#ifdef _ENABLE_IMGUI
#include "Debug/UIBus.h"
#endif

#ifdef SHOW_DX_LIVE_OBJECT
#include <dxgi1_3.h>
#include <dxgidebug.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#endif //_DEBUG

namespace SFW
{
	namespace Graphics::DX11
	{
		GraphicsDevice::~GraphicsDevice()
		{
			// ここでレンダースレッドを停止
			StopRenderThread();

#ifdef SHOW_DX_LIVE_OBJECT
			// まずはバインド解除と状態クリア
			m_context->ClearState();
			m_context->Flush();

			// 1) D3D11 側（型別・参照カウントつきで詳細）
			ComPtr<ID3D11Debug> d3dDebug;
			if (SUCCEEDED(m_device.As(&d3dDebug))) {
				d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			}

			// 2) DXGI 側（モジュール横断）
			ComPtr<IDXGIDebug1> dxgiDebug;
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
				dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			}
#endif //_DEBUG
		}

		GraphicsDevice::GraphicsDevice(GraphicsDevice&& rhs) noexcept
		{
			m_device = rhs.m_device;
			m_context = rhs.m_context;
			m_swapChain = rhs.m_swapChain;
			m_renderTargetView = rhs.m_renderTargetView;
			m_depthStencilBuffer = rhs.m_depthStencilBuffer;
			m_depthStencilView = rhs.m_depthStencilView;

			*this = std::move(rhs);
		}

		GraphicsDevice& GraphicsDevice::operator=(GraphicsDevice&& rhs) noexcept
		{
			if (this == &rhs) return *this;

			// 自分側に既存ランナーがいれば止める（所有を捨てる前にクリーンアップ）
			StopRenderThread();

			// COM とスワップチェーンなどを移譲
			m_device = rhs.m_device;
			m_context = rhs.m_context;
			m_swapChain = rhs.m_swapChain;
			m_renderTargetView = rhs.m_renderTargetView;
			m_depthStencilBuffer = rhs.m_depthStencilBuffer;
			m_depthStencilView = rhs.m_depthStencilView;

			// 共有状態を移譲し、owner を新しい this へ
			m_rt = std::move(rhs.m_rt);
			if (m_rt) m_rt->owner.store(this, std::memory_order_release);

			// rhs をヌル化
			rhs.m_rt.reset();
			return *this;
		}

		bool GraphicsDevice::InitializeImpl(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height, double fps)
		{
			if (!std::holds_alternative<HWND>(nativeWindowHandle)) return false;
			HWND hWnd = std::get<HWND>(nativeWindowHandle);

			DXGI_SWAP_CHAIN_DESC scDesc = {};
			scDesc.BufferCount = 2;
			scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			scDesc.BufferDesc.Width = width;
			scDesc.BufferDesc.Height = height;
			scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			scDesc.OutputWindow = hWnd;
			scDesc.SampleDesc.Count = 1;
			scDesc.Windowed = TRUE;
			scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			UINT createDeviceFlags = 0;

#if defined(_DEBUG)
			createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

			D3D_FEATURE_LEVEL featureLevel;
			const D3D_FEATURE_LEVEL featureLevels[] = {
				D3D_FEATURE_LEVEL_11_0
			};

			HRESULT hr = D3D11CreateDeviceAndSwapChain(
				nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				createDeviceFlags, featureLevels, 1, D3D11_SDK_VERSION,
				&scDesc, m_swapChain.GetAddressOf(),
				m_device.GetAddressOf(), &featureLevel,
				m_context.GetAddressOf()
			);

			if (FAILED(hr)) {
				LOG_ERROR("Failed to create D3D11 device and swap chain: %d", hr);
				return false;
			}

			// バックバッファ取得
			Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
			hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(hr)) {
				LOG_ERROR("Failed to get back buffer: %d", hr);
				return false;
			}

			// RenderTargetView 作成
			// RTV は SRGB で作成（ガンマ書き込みが有効）
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

			hr = m_device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, m_renderTargetView.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create render target view: %d", hr);
				return false;
			}

			// 出力先としてセット
			m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

			// ビューポート設定
			m_viewport.Width = static_cast<FLOAT>(width);
			m_viewport.Height = static_cast<FLOAT>(height);
			m_viewport.MinDepth = 0.0f;
			m_viewport.MaxDepth = 1.0f;
			m_viewport.TopLeftX = 0;
			m_viewport.TopLeftY = 0;
			m_context->RSSetViewports(1, &m_viewport);

			// 深度ステンシルバッファの作成
			D3D11_TEXTURE2D_DESC depthDesc = {};
			depthDesc.Width = width;
			depthDesc.Height = height;
			depthDesc.MipLevels = 1;
			depthDesc.ArraySize = 1;
			depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
			depthDesc.SampleDesc.Count = 1;
			depthDesc.SampleDesc.Quality = 0;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

			hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil buffer: %d", hr);
				return false;
			}

			// 深度ステンシルビューの作成
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.Flags = 0;

			hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil view: %d", hr);
				return false;
			}

			// 読み取り専用深度ステンシルビューの作成
			dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
			hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilViewReadOnly.GetAddressOf());

			// 深度ステンシルバッファのシェーダーリソースビュー作成
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc, m_depthStencilSRV.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil shader resource view: %d", hr);
				return false;
			}

			//ResourceManager関連初期化
			//===========================================================
			meshManager = std::make_unique<MeshManager>(m_device.Get());
			shaderManager = std::make_unique<ShaderManager>(m_device.Get());
			textureManager = std::make_unique<TextureManager>(m_device.Get(), m_context.Get());
			bufferManager = std::make_unique<BufferManager>(m_device.Get(), m_context.Get());
			samplerManager = std::make_unique<SamplerManager>(m_device.Get());
			materialManager = std::make_unique<MaterialManager>(shaderManager.get(), textureManager.get(), bufferManager.get(), samplerManager.get());
			psoManager = std::make_unique<PSOManager>(m_device.Get(), shaderManager.get());

			modelAssetManager = std::make_unique<ModelAssetManager>(*meshManager, *materialManager, *shaderManager, *psoManager, *textureManager, *bufferManager, *samplerManager, m_device.Get());

			backend = std::make_unique<RenderBackend>(
				m_device.Get(), m_context.Get(),
				meshManager.get(), materialManager.get(), shaderManager.get(), psoManager.get(),
				textureManager.get(), bufferManager.get(), samplerManager.get(), modelAssetManager.get()
			);

			//Masked Occlution Culling初期化
			//==============================================================
			MOC* moc = MOC::Create();
			if (!moc) {
				assert(false && "Failed to create MOC instance");
			}

			moc->SetResolution(width, height);
			moc->ClearBuffer();
			moc->SetNearClipPlane(0.1f);

			renderGraph = std::make_unique<RenderGraph>(*backend, moc);

			// ここでレンダースレッドを起動
			StartRenderThread();

#ifdef _ENABLE_IMGUI
			m_gpuUtilPDH.init();
			m_lastSampledPID = GetCurrentProcessId();

			m_gpuTimer.init(m_device.Get(), RENDER_BUFFER_COUNT);
			m_gpuTimeBudget = 1.0f / fps;
#endif

			return true;
		}

		void GraphicsDevice::ClearImpl(const FLOAT clearColor[4])
		{
#if CLEAR_MAIN_RENDER_TARGET
			m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
#endif
			m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}

		void GraphicsDevice::DrawImpl()
		{
#ifdef _ENABLE_IMGUI
			//GPU使用率を測定開始
			m_gpuTimer.begin(m_context.Get());
			//経過時間計測用
			auto t0 = std::chrono::steady_clock::now();
#endif

			renderGraph->Execute();

#ifdef _ENABLE_IMGUI
			auto t1 = std::chrono::steady_clock::now();
			double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

			// 描画系のCPUの実行時間をデバッグ情報に送信
			Debug::PublishRenderMs(float(ms));

			constexpr double sampleInterval = 0.5; // サンプリング間隔（秒）

			// 一定時間ごとにGPU使用率を取得してUIに送る
			m_elapsedTime += m_gpuTimeBudget;
			if(m_elapsedTime > sampleInterval)
			{
				m_elapsedTime = 0.0f;

				double pct = 0.0;
				if(m_gpuUtilPDH.sample(m_lastSampledPID, pct))
				{
					//GPU使用率をUIに送る
					Debug::PublishGpu(float(pct / 100));
				}
			}

			m_gpuTimer.end(m_context.Get());
			double gpuSec = m_gpuTimer.tryResolve(m_context.Get());
			if (gpuSec >= 0.0) {
				// GPU経過時間をデバッグ情報に送信
				Debug::PublishGpuFrameMs(float(gpuSec * 1000.0));
			}

			//Imguiのためにメインのターゲットに戻す
			SetMainRenderTargetAndDepth();
#endif
		}

		void GraphicsDevice::PresentImpl()
		{


			m_swapChain->Present(1, 0);
		}

		// ==== レンダースレッド API ====
		void GraphicsDevice::StartRenderThread() {
			if (m_rt && m_rt->running.load()) return;
			if (!m_rt) m_rt = std::make_shared<RTState>();
			m_rt->owner.store(this, std::memory_order_release);
			m_rt->running.store(true, std::memory_order_release);

			// 既存 thread を再利用しない（ムーブ可能にするため shared_ptr で捕捉）
			m_rt->thread = std::thread([st = m_rt] { st->owner.load()->RenderThreadMain(st); });
		}

		void GraphicsDevice::StopRenderThread() {
			if (!m_rt) return;
			if (!m_rt->running.exchange(false)) {
				// 既に止まっている
				if (m_rt->thread.joinable()) m_rt->thread.join();
				return;
			}
			// 起床
			{
				std::lock_guard<std::mutex> lk(m_rt->qMtx);
				// 何もしない（キューが空でも起こすための notify）
			}
			m_rt->qCv.notify_all();

			if (m_rt->thread.joinable()) m_rt->thread.join();
		}

		void GraphicsDevice::SubmitFrameImpl(const FLOAT clearColor[4], uint64_t frameIdx) {
			auto st = m_rt;
			if (!st) return;

			// === バックプレッシャ：RENDER_QUEUE_BUFFER_COUNT 枠を超えない ===
			// 条件: (lastSubmitted - lastCompleted) >= MaxInFlight
			{
				std::unique_lock<std::mutex> lk(st->doneMtx);
				st->doneCv.wait(lk, [&] {
					const uint64_t sub = st->lastSubmitted.load(std::memory_order_acquire);
					const uint64_t cmp = st->lastCompleted.load(std::memory_order_acquire);
					return (sub - cmp) < RTState::MaxInFlight;
					});
			}

			RenderSubmit job{};
			memcpy(job.clearColor, clearColor, sizeof(FLOAT) * 4);
			job.frameIdx = frameIdx;
			job.doClear = true;

			{
				std::lock_guard<std::mutex> lk(st->qMtx);
				st->queue.emplace_back(job);
				st->lastSubmitted.fetch_add(1, std::memory_order_release);
			}
			st->qCv.notify_one();
		}

		void GraphicsDevice::WaitSubmittedFramesImpl(uint64_t uptoFrame) {
			auto st = m_rt;
			if (!st) return;
			std::unique_lock<std::mutex> lk(st->doneMtx);
			st->doneCv.wait(lk, [&] { return st->lastCompleted.load(std::memory_order_acquire) >= uptoFrame; });
		}

		void GraphicsDevice::SetMainRenderTargetAndDepth()
		{
			backend->SetRenderTargets({m_renderTargetView.Get()}, m_depthStencilView.Get());
		}

		void GraphicsDevice::SetMainRenderTargetNoDepth()
		{
			backend->SetRenderTargets({ m_renderTargetView.Get() }, nullptr);
		}


		void GraphicsDevice::SetBlendState(BlendStateID state)
		{
			backend->SetBlendState(state);
		}

		void GraphicsDevice::SetDepthStencilState(DepthStencilStateID state, UINT stencilRef)
		{
			backend->SetDepthStencilState(state, stencilRef);
		}

		void GraphicsDevice::SetRasterizerState(RasterizerStateID state)
		{
			backend->SetRasterizerState(state);
		}

		void GraphicsDevice::RenderThreadMain(std::shared_ptr<RTState> st) {
			// Immediate Context はこのスレッド専有
			while (st->running.load(std::memory_order_acquire)) {
				RenderSubmit job{};
				{
					std::unique_lock<std::mutex> lk(st->qMtx);
					st->qCv.wait(lk, [&] { return !st->queue.empty() || !st->running.load(); });
					if (!st->running.load() && st->queue.empty()) break;
					job = st->queue.front();
					st->queue.pop_front();
				}

				// 実行
				if (job.doClear) Clear(job.clearColor);
				Draw();
				Present();

				// 完了を記録して通知
				{
					std::lock_guard<std::mutex> lk(st->doneMtx);
					st->lastCompleted.fetch_add(1, std::memory_order_release);
				}
				st->doneCv.notify_all();
			}
		}
	}
}