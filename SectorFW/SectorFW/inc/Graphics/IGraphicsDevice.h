/*****************************************************************//**
 * @file   IGraphicsDevice.h
 * @brief  グラフィックデバイスのインターフェースクラスのまとめ
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#include <wrl/client.h>

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
class IGraphicsCommandList {
public:
	/**
	 * @brief テクスチャのセット
	 * @param texture テクスチャ
	 */
	virtual void SetTexture(ITexture* texture) = 0;
	/**
	 * @brief バッファのセット
	 * @param vb バッファ
	 * @param offset オフセット
	 */
	virtual void SetVertexBuffer(IVertexBuffer* vb, UINT offset) = 0;
	/**
	 * @brief 描画
	 * @param vertexCount 頂点数
	 * @param startVertexLocation 頂点開始地点
	 */
	virtual void Draw(UINT vertexCount, UINT startVertexLocation = 0) = 0;
	virtual ~IGraphicsCommandList() = default;
};

/**
 * @brief グラフィックコマンドリストのCRTPクラス
 * @tparam Impl 実装クラス
 */
template <typename Impl>
class GraphicsCommandListCRTP : public IGraphicsCommandList {
public:
	/**
	 * @brief テクスチャのセット
	 * @param texture テクスチャ
	 */
	void SetTexture(ITexture* texture) override {
		static_cast<Impl*>(this)->SetTextureImpl(texture);
	}
	/**
	 * @brief バッファのセット
	 * @param vb バッファ
	 * @param offset オフセット
	 */
	void SetVertexBuffer(IVertexBuffer* vb, UINT offset = 0) override {
		static_cast<Impl*>(this)->SetVertexBufferImpl(vb, offset);
	}
	/**
	 * @brief 描画
	 * @param vertexCount 頂点数
	 * @param startVertexLocation 開始地点
	 */
	void Draw(UINT vertexCount, UINT startVertexLocation = 0) override {
		static_cast<Impl*>(this)->DrawImpl(vertexCount);
	}
};

/**
 * @brief グラフィックデバイスのインターフェース
 * @class IGraphicsDevice
 */
class IGraphicsDevice {
protected:
	/**
	 * @brief 初期化
	 * @param nativeWindowHandle ウィンドウハンドル
	 * @param width ウィンドウ幅
	 * @param height ウィンドウ高さ
	 */
	virtual bool Initialize(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height) = 0;
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
	virtual void Clear(const float clearColor[4]) = 0;
	/**
	 * @brief 描画
	 */
	virtual void Present() = 0;
	/**
	 * @brief 初期化
	 * @param hwnd ウィンドウハンドル
	 * @param width ウィンドウ幅
	 * @param height ウィンドウ高さ
	 */
	virtual std::shared_ptr<IGraphicsCommandList> CreateCommandList() = 0;
	/**
	 * @brief テクスチャの作成
	 * @param path テクスチャのパス
	 */
	virtual std::shared_ptr<ITexture> CreateTexture(const std::string& path) = 0;
	/**
	 * @brief バーテックスバッファの作成
	 * @param data データ
	 * @param size サイズ
	 * @param stride ストライド
	 */
	virtual std::shared_ptr<IVertexBuffer> CreateVertexBuffer(const void* data, size_t size, UINT stride) = 0;
	/**
	 * @brief デストラクタ
	 */
	virtual ~IGraphicsDevice() = default;
	/**
	 * @brief 初期化フラグの取得
	 * @return 初期化フラグ
	 */
	bool IsInitialized() const { return m_isInitialized; }
private:
	// 初期化フラグ
	bool m_isInitialized = false;
};
