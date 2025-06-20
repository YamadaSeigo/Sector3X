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

// ウィンドウハンドルの型
//======================================================================
using NativeWindowHandle = std::variant<HWND>;
//======================================================================

/**
 * @brief テクスチャのインターフェース
 * @class ITexture
 */
class ITexture {
public:
	/**
	 * @brief デストラクタ
	 */
	virtual ~ITexture() = default;
};

/**
 * @brief バーテックスバッファのインターフェース
 * @class IVertexBuffer
 */
class IVertexBuffer {
public:
	/**
	 * @brief デストラクタ
	 */
	virtual ~IVertexBuffer() = default;
};

/**
 * @brief グラフィックコマンドリストインターフェース
 * @class IGraphicsCommandList
 */
template<typename Impl>
class IGraphicsCommandList {
public:
	/**
	 * @brief テクスチャのセット
	 * @param texture テクスチャ
	 */
	void SetTexture(ITexture* texture) {
		static_cast<Impl*>(this)->SetTextureImpl(texture);
	}
	/**
	 * @brief バッファのセット
	 * @param vb バッファ
	 * @param offset オフセット
	 */
	void SetVertexBuffer(IVertexBuffer* vb, UINT offset = 0) {
		static_cast<Impl*>(this)->SetVertexBufferImpl(vb, offset);
	}
	/**
	 * @brief 描画
	 * @param vertexCount 頂点数
	 * @param startVertexLocation 開始地点
	 */
	void Draw(UINT vertexCount, UINT startVertexLocation = 0) {
		static_cast<Impl*>(this)->DrawImpl(vertexCount);
	}
};

template<typename Impl>
concept GraphicsCommandType = std::derived_from<Impl, IGraphicsCommandList<Impl>>;

/**
 * @brief グラフィックデバイスのインターフェース
 * @class IGraphicsDevice
 */
template<typename Impl, GraphicsCommandType GraphicsCommand>
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
		m_isInitialized = Initialize(nativeWindowHandle, width, height);
	}
	/**
	 * @brief 画面をクリア
	 * @param clearColor
	 */
	void Clear(const float clearColor[4])	{
		static_cast<Impl*>(this)->ClearImpl(clearColor);
	}
	/**
	 * @brief 描画
	 */
	void Present() {
		static_cast<Impl*>(this)->PresentImpl();
	}
	/**
	 * @brief 初期化
	 * @param hwnd ウィンドウハンドル
	 * @param width ウィンドウ幅
	 * @param height ウィンドウ高さ
	 */
	std::shared_ptr<GraphicsCommand> CreateCommandList() {
		return static_cast<Impl*>(this)->CreateCommandListImpl();
	}
	/**
	 * @brief テクスチャの作成
	 * @param path テクスチャのパス
	 */
	std::shared_ptr<ITexture> CreateTexture(const std::string& path) {
		return static_cast<Impl*>(this)->CreateTextureImpl(path);
	}
	/**
	 * @brief バーテックスバッファの作成
	 * @param data データ
	 * @param size サイズ
	 * @param stride ストライド
	 */
	std::shared_ptr<IVertexBuffer> CreateVertexBuffer(const void* data, size_t size, UINT stride) {
		return static_cast<Impl*>(this)->CreateVertexBufferImpl(data, size, stride);
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
	bool m_isInitialized = false;
};
