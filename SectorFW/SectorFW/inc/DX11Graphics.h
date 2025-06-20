#pragma once

#include "Graphics/IGraphicsDevice.h"

#include <d3d11.h>
#pragma comment(lib,"d3d11.lib")

namespace SectorFW
{
	/**
	 * @brief DX11用テクスチャクラス
	 * @class DX11Texture
	 */
	class DX11Texture : public ITexture {
	public:
		DX11Texture(ComPtr<ID3D11ShaderResourceView> srv)
			: m_srv(srv) {
		}

		ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }

	private:
		// シェーダーリソースビュー
		ComPtr<ID3D11ShaderResourceView> m_srv;
	};

	/**
	 * @brief DX11用バーテックスバッファークラス
	 * @class DX11VertexBuffer
	 */
	class DX11VertexBuffer : public IVertexBuffer {
	public:
		/**
		 * @brief コンストラクタ
		 * @param buffer バーテックスバッファ
		 * @param stride　ストライド
		 */
		DX11VertexBuffer(ComPtr<ID3D11Buffer> buffer, UINT stride)
			: m_buffer(buffer), m_stride(stride) {
		}
		/**
		 * @brief バッファの取得
		 * @return m_buffer バッファ
		 */
		ID3D11Buffer* GetBuffer() const { return m_buffer.Get(); }
		/**
		 * @brief ストライドの取得
		 * @return m_stride ストライド
		 */
		UINT GetStride() const { return m_stride; }

	private:
		// バーテックスバッファー
		ComPtr<ID3D11Buffer> m_buffer;
		// ストライド
		UINT m_stride;
	};

	/**
	 * @brief DX11用コマンドリストクラス
	 * @class DX11CommandListImpl
	 */
	class DX11CommandListImpl : public IGraphicsCommandList<DX11CommandListImpl> {
	public:
		/**
		 * @brief コンストラクタ
		 * @param context デバイスコンテキスト
		 */
		DX11CommandListImpl(ID3D11DeviceContext* context)
			: m_context(context) {
		}
		/**
		 * @brief テクスチャをセットする
		 * @param texture セットするテクスチャ
		 */
		void SetTextureImpl(ITexture* texture) {
			auto dxTexture = static_cast<DX11Texture*>(texture);
			ID3D11ShaderResourceView* srv = dxTexture->GetSRV();
			m_context->PSSetShaderResources(0, 1, &srv);
		}
		/**
		 * @brief バーテックスバッファーを指定する
		 * @param vb セットするバーテックスバッファー
		 * @param offset　セットするバッファのオフセット
		 */
		void SetVertexBufferImpl(IVertexBuffer* vb, UINT offset) {
			auto dxvb = static_cast<DX11VertexBuffer*>(vb);
			ID3D11Buffer* buffer = dxvb->GetBuffer();
			UINT stride = dxvb->GetStride();
			m_context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
		}
		/**
		 * @brief プリミティブトポロジを指定する
		 * @param topology トポロジ
		 */
		void DrawImpl(UINT vertexCount, UINT startVertexLocation = 0) {
			m_context->Draw(vertexCount, startVertexLocation);
		}

	private:
		// デバイスコンテキスト
		ID3D11DeviceContext* m_context; // 借用：解放しない
	};

	/**
	 * @brief DX11用グラフィックスデバイスクラス
	 * @class DX11GraphicsDevice
	 */
	class DX11GraphicsDevice : public IGraphicsDevice<DX11GraphicsDevice, DX11CommandListImpl>
	{
		using CommandList = DX11CommandListImpl;

	public:
		/**
		 * @brief コンストラクタ
		 */
		DX11GraphicsDevice() = default;
		/**
		 * @brief デストラクタ
		 */
		~DX11GraphicsDevice() = default;

		bool InitializeImpl(const NativeWindowHandle& nativeWindowHandle, uint32_t width, uint32_t height);
		void ClearImpl(const FLOAT clearColor[4]);
		void PresentImpl();
		/**
		 * @brief コマンドリストを取得
		 * @return コマンドリスト
		 */
		std::shared_ptr<DX11CommandListImpl> CreateCommandListImpl();
		/**
		 * @brief テクスチャを生成
		 * @return テクスチャインターフェース
		 */
		std::shared_ptr<ITexture> CreateTextureImpl(const std::string& path);
		/**
		 * @brief バーテックスバッファーを生成
		 * @return バーテックスバッファーインターフェース
		 */
		std::shared_ptr<IVertexBuffer> CreateVertexBufferImpl(const void* data, size_t size, UINT stride);

	private:
		// デバイス
		ComPtr<ID3D11Device> m_device;
		// デバイスコンテキスト
		ComPtr<ID3D11DeviceContext> m_context;
		ComPtr<IDXGISwapChain> m_swapChain;
		ComPtr<ID3D11RenderTargetView> m_renderTargetView;
	};
}
