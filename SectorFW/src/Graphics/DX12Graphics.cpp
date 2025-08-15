#include "DX12Graphics.h"

namespace SectorFW
{
	//bool DX12GraphicsDevice::Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height)
	//{
	//	if (!std::holds_alternative<HWND>(nativeWindowHandle)) return false;
	//	HWND hWnd = std::get<HWND>(nativeWindowHandle);

	//	HRESULT hr;

	//	// DXGIファクトリ & アダプタ取得
	//	ComPtr<IDXGIFactory6> factory;
	//	hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	//	if (FAILED(hr)) return false;

	//	ComPtr<IDXGIAdapter1> adapter;
	//	hr = factory->EnumAdapters1(0, &adapter);
	//	if (FAILED(hr)) return false;

	//	// デバイス作成
	//	hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	//	if (FAILED(hr)) return false;

	//	// コマンドキュー作成
	//	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	//	hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
	//	if (FAILED(hr)) return false;

	//	// スワップチェーン作成
	//	DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
	//	swapDesc.BufferCount = FrameCount;
	//	swapDesc.Width = 1280;
	//	swapDesc.Height = 720;
	//	swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	//	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	//	swapDesc.SampleDesc.Count = 1;

	//	ComPtr<IDXGISwapChain1> tempSwapChain;
	//	hr = factory->CreateSwapChainForHwnd(commandQueue.Get(), hWnd, &swapDesc, nullptr, nullptr, &tempSwapChain);
	//	if (FAILED(hr)) return false;

	//	tempSwapChain.As(&swapChain);
	//	frameIndex = swapChain->GetCurrentBackBufferIndex();

	//	// RTVヒープ
	//	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	//	rtvHeapDesc.NumDescriptors = FrameCount;
	//	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	//	hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
	//	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//	if (FAILED(hr)) return false;

	//	// バックバッファ取得 & RTV作成
	//	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	//	for (UINT i = 0; i < FrameCount; i++) {
	//		swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
	//		device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
	//		rtvHandle.ptr += rtvDescriptorSize;
	//	}

	//	// コマンドアロケータ & リスト
	//	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	//	if (FAILED(hr)) return false;

	//	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	//	if (FAILED(hr)) return false;

	//	hr = commandList->Close();
	//	if (FAILED(hr)) return false;

	//	// フェンス
	//	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	//	if (FAILED(hr)) return false;

	//	fenceValue = 1;
	//	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	//	return true;
	//}

	//void DX12GraphicsDevice::Clear(const FLOAT clearColor[4])
	//{
	//	// Reset allocator and command list
	//	commandAllocator->Reset();
	//	commandList->Reset(commandAllocator.Get(), nullptr);

	//	// Transition: Present -> RenderTarget
	//	D3D12_RESOURCE_BARRIER barrier = {};
	//	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	//	barrier.Transition.pResource = renderTargets[frameIndex].Get();
	//	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	//	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	//	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	//	commandList->ResourceBarrier(1, &barrier);

	//	// Set render target
	//	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	//	rtvHandle.ptr += frameIndex * rtvDescriptorSize;
	//	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	//	// Clear screen
	//	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	//}

	//void DX12GraphicsDevice::Present()
	//{
	//	// Transition: RenderTarget -> Present
	//	D3D12_RESOURCE_BARRIER barrier = {};
	//	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	//	barrier.Transition.pResource = renderTargets[frameIndex].Get();
	//	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	//	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	//	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	//	commandList->ResourceBarrier(1, &barrier);

	//	// Close and execute
	//	commandList->Close();
	//	ID3D12CommandList* cmdLists[] = { commandList.Get() };
	//	commandQueue->ExecuteCommandLists(1, cmdLists);

	//	// Present
	//	swapChain->Present(1, 0);

	//	// Sync
	//	const UINT64 currentFence = ++fenceValue;
	//	commandQueue->Signal(fence.Get(), currentFence);
	//	if (fence->GetCompletedValue() < currentFence) {
	//		fence->SetEventOnCompletion(currentFence, fenceEvent);
	//		WaitForSingleObject(fenceEvent, INFINITE);
	//	}

	//	// Update frame index
	//	frameIndex = swapChain->GetCurrentBackBufferIndex();
	//}

	//std::shared_ptr<IGraphicsCommandList> DX12GraphicsDevice::CreateCommandList()
	//{
	//	return std::shared_ptr<IGraphicsCommandList>();
	//}

	//std::shared_ptr<ITexture> DX12GraphicsDevice::CreateTexture(const std::string& path)
	//{
	//	return std::shared_ptr<ITexture>();
	//}

	//std::shared_ptr<IVertexBuffer> DX12GraphicsDevice::CreateVertexBuffer(const void* data, size_t size, UINT stride)
	//{
	//	return std::shared_ptr<IVertexBuffer>();
	//}
}