#include "DX11Graphics.h"

#include "Util/logger.h"

#include "Math/convert.hpp"

namespace SectorFW
{
	namespace Graphics
	{
		bool DX11GraphicsDevice::InitializeImpl(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height)
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
			hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create render target view: %d", hr);
				return false;
			}

			// 出力先としてセット
			m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

			// ビューポート設定
			D3D11_VIEWPORT vp = {};
			vp.Width = static_cast<FLOAT>(width);
			vp.Height = static_cast<FLOAT>(height);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			vp.TopLeftX = 0;
			vp.TopLeftY = 0;
			m_context->RSSetViewports(1, &vp);

			// 深度ステンシルバッファの作成
			D3D11_TEXTURE2D_DESC depthDesc = {};
			depthDesc.Width = width;
			depthDesc.Height = height;
			depthDesc.MipLevels = 1;
			depthDesc.ArraySize = 1;
			depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 一般的な深度+ステンシル形式
			depthDesc.SampleDesc.Count = 1;
			depthDesc.SampleDesc.Quality = 0;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

			hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil buffer: %d", hr);
				return false;
			}

			// 深度ステンシルビューの作成
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Format = depthDesc.Format;
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;

			hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create depth stencil view: %d", hr);
				return false;
			}

			return true;
		}

		void DX11GraphicsDevice::ClearImpl(const FLOAT clearColor[4])
		{
			m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
			m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}

		void DX11GraphicsDevice::DrawImpl()
		{
			auto& renderGraph = GetRenderGraph();
			renderGraph.Execute();
		}

		void DX11GraphicsDevice::PresentImpl()
		{
			m_swapChain->Present(1, 0);
		}

		void DX11GraphicsDevice::TestInitialize()
		{
			auto& graph = GetRenderGraph();
			std::vector<ID3D11RenderTargetView*> rtvs{
				m_renderTargetView.Get() };

			auto constantMgr = GetRenderGraph().GetRenderService()->GetResourceManager<DX11BufferManager>();
			auto cameraHandle = constantMgr->FindByName(DX113DCameraService::BUFFER_NAME);

			graph.AddPass("TestPass", rtvs, m_depthStencilView.Get(), { cameraHandle });
		}

		inline DX11GraphicsDevice::DX11RenderGraph& DX11GraphicsDevice::GetRenderGraph()
		{
			static DX11MeshManager meshManager(m_device.Get());
			static DX11ShaderManager shaderManager(m_device.Get());
			static DX11TextureManager textureManager(m_device.Get());
			static DX11BufferManager cbManager(m_device.Get(), m_context.Get());
			static DX11SamplerManager samplerManager(m_device.Get());
			static DX11MaterialManager materialManager(&shaderManager, &textureManager, &cbManager, &samplerManager);
			static DX11PSOManager psoManager(m_device.Get(), &shaderManager);
			static DX11ModelAssetManager modelAssetManager(meshManager, materialManager, shaderManager, textureManager, cbManager, samplerManager, m_device.Get());

			static DX11Backend backend = DX11Backend(
				m_device.Get(), m_context.Get(),
				&meshManager, &materialManager, &shaderManager, &psoManager,
				&textureManager, &cbManager, &samplerManager, &modelAssetManager
			);

			static DX11RenderGraph m_renderGraph = DX11RenderGraph(backend);
			return m_renderGraph;
		}
	}
}