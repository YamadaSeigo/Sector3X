#include "DX11Graphics.h"

namespace SectorFW
{
	bool DX11GraphicsDevice::Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height)
	{
		if (!std::holds_alternative<HWND>(nativeWindowHandle)) return false;
		HWND hWnd = std::get<HWND>(nativeWindowHandle);

		DXGI_SWAP_CHAIN_DESC scDesc = {};
		scDesc.BufferCount = 1;
		scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.OutputWindow = hWnd;
		scDesc.SampleDesc.Count = 1;
		scDesc.Windowed = TRUE;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

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
			return false;
		}

		// バックバッファ取得
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(hr)) return false;

		// RenderTargetView 作成
		hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
		if (FAILED(hr)) return false;

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

		return true;
	}

	void DX11GraphicsDevice::Clear(const FLOAT clearColor[4])
	{
		m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
	}

	void DX11GraphicsDevice::Present()
	{
		m_swapChain->Present(1, 0);
	}

	std::shared_ptr<IGraphicsCommandList> DX11GraphicsDevice::CreateCommandList()
	{
		return std::shared_ptr<DX11CommandListImpl>();
	}

	std::shared_ptr<ITexture> DX11GraphicsDevice::CreateTexture(const std::string& path)
	{
		return std::shared_ptr<DX11Texture>();
	}

	std::shared_ptr<IVertexBuffer> DX11GraphicsDevice::CreateVertexBuffer(const void* data, size_t size, UINT stride)
	{
		return std::shared_ptr<DX11VertexBuffer>();
	}
}