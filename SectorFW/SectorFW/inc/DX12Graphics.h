#pragma once

#include "Graphics/IGraphicsDevice.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace SectorFW
{
	//class DX12GraphicsDevice : public IGraphicsDevice<DX12GraphicsDevice>
	//{
	//public:
	//	/**
	//	 * @brief コンストラクタ
	//	 */
	//	DX12GraphicsDevice() = default;
	//	/**
	//	 * @brief デストラクタ
	//	 */
	//	~DX12GraphicsDevice() = default;

	//	bool Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height) override;
	//	void Clear(const FLOAT clearColor[4]) override;
	//	void Present() override;
	//	/**
	//	 * @brief コマンドリストを取得
	//	 * @return コマンドリスト
	//	 */
	//	virtual std::shared_ptr<IGraphicsCommandList> CreateCommandList() override;
	//	/**
	//	 * @brief テクスチャを生成
	//	 * @return テクスチャインターフェース
	//	 */
	//	virtual std::shared_ptr<ITexture> CreateTexture(const std::string& path) override;
	//	/**
	//	 * @brief バーテックスバッファーを生成
	//	 * @return バーテックスバッファーインターフェース
	//	 */
	//	virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(const void* data, size_t size, UINT stride) override;

	//private:
	//	static constexpr inline int FrameCount = 2;

	//	ComPtr<ID3D12Device> device;
	//	ComPtr<IDXGISwapChain3> swapChain;
	//	ComPtr<ID3D12CommandQueue> commandQueue;
	//	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	//	ComPtr<ID3D12Resource> renderTargets[FrameCount];
	//	ComPtr<ID3D12CommandAllocator> commandAllocator;
	//	ComPtr<ID3D12GraphicsCommandList> commandList;
	//	ComPtr<ID3D12Fence> fence;
	//	HANDLE fenceEvent;
	//	UINT64 fenceValue = 0;
	//	UINT rtvDescriptorSize;
	//	UINT frameIndex;
	//};
}