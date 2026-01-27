#include "Graphics/DX11/DX11LightShadowResourceService.h"

namespace SFW
{
	namespace Graphics::DX11
	{
		bool LightShadowResourceService::Initialize(ID3D11Device* device,
			const ShadowMapConfig& cfg)
		{
			if (!device) return false;

			m_config = cfg;
			m_cascadeCount = std::max<std::uint32_t>(1,
				std::min<std::uint32_t>(cfg.cascadeCount, kMaxShadowCascades));

			return CreateResources(device);
		}

		bool LightShadowResourceService::Resize(ID3D11Device* device,
			std::uint32_t width,
			std::uint32_t height)
		{
			if (!device) return false;
			m_config.width = width;
			m_config.height = height;
			return CreateResources(device);
		}

		bool LightShadowResourceService::CreateResources(ID3D11Device* device)
		{
			// 既存を破棄（ComPtr は代入でリセットされる）
			m_shadowTex.Reset();
			m_shadowSRV.Reset();

			for (auto& dsv : m_cascadeDSV) dsv.Reset();
			m_shadowSampler.Reset();
			m_shadowRS.Reset();
			m_cbShadowCascades.Reset();

			m_pointLightBuffer.Reset();
			m_pointLightSRV.Reset();

			HRESULT hr;

			// ---------- 1) Texture2DArray ----------
			D3D11_TEXTURE2D_DESC texDesc = {};
			texDesc.Width = m_config.width;
			texDesc.Height = m_config.height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = m_cascadeCount;
			texDesc.Format = m_config.texFormat; // R32_TYPELESS 推奨
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			texDesc.CPUAccessFlags = 0;
			texDesc.MiscFlags = 0;

			hr = device->CreateTexture2D(&texDesc, nullptr, &m_shadowTex);
			if (FAILED(hr)) return false;

			// ---------- 2) DSV (各カスケードごと) ----------
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Format = m_config.dsvFormat;
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsvDesc.Flags = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.Texture2DArray.ArraySize = 1;

			for (std::uint32_t i = 0; i < m_cascadeCount; ++i)
			{
				dsvDesc.Texture2DArray.FirstArraySlice = i;
				hr = device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, &m_cascadeDSV[i]);
				if (FAILED(hr)) return false;
			}

			// Viewport もここで設定しておく
			D3D11_VIEWPORT vp{};
			vp.TopLeftX = 0.0f;
			vp.TopLeftY = 0.0f;
			vp.Width = static_cast<float>(m_config.width);
			vp.Height = static_cast<float>(m_config.height);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			m_cascadeViewport = vp;

			// ---------- 3) SRV ----------
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = m_config.srvFormat; // R32_FLOAT
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = m_cascadeCount;

			hr = device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, &m_shadowSRV);
			if (FAILED(hr)) return false;

			// ---------- 4) 比較サンプラ ----------
			{
				D3D11_SAMPLER_DESC sampDesc = {};
				sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
				sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
				sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
				sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
				sampDesc.BorderColor[0] = 1.0f;
				sampDesc.BorderColor[1] = 1.0f;
				sampDesc.BorderColor[2] = 1.0f;
				sampDesc.BorderColor[3] = 1.0f;
				sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
				sampDesc.MinLOD = 0.0f;
				sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

				hr = device->CreateSamplerState(&sampDesc, &m_shadowSampler);
				if (FAILED(hr)) return false;
			}

			// ---------- 5) DepthBias ラスタライザ ----------
			{
				D3D11_RASTERIZER_DESC rsDesc = {};
				rsDesc.FillMode = D3D11_FILL_SOLID;
				rsDesc.CullMode = D3D11_CULL_BACK; // 必要に応じて FRONT/ NONE
				rsDesc.FrontCounterClockwise = TRUE;
				rsDesc.DepthBias = 0;   // 要調整
				rsDesc.SlopeScaledDepthBias = 0.0f;   // 要調整
				rsDesc.DepthBiasClamp = 0.0f;
				rsDesc.DepthClipEnable = FALSE;
				rsDesc.ScissorEnable = FALSE;
				rsDesc.MultisampleEnable = FALSE;
				rsDesc.AntialiasedLineEnable = FALSE;

				hr = device->CreateRasterizerState(&rsDesc, &m_shadowRS);
				if (FAILED(hr)) return false;
			}

			// ---------- 6) 定数バッファ ----------
			{
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = sizeof(CBShadowCascadesData);
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				bd.MiscFlags = 0;

				hr = device->CreateBuffer(&bd, nullptr, &m_cbShadowCascades);
				if (FAILED(hr)) return false;
			}

			{
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = sizeof(CPULightData);
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				bd.MiscFlags = 0;

				hr = device->CreateBuffer(&bd, nullptr, &m_cbLightData);
				if (FAILED(hr)) return false;
			}

			{
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = sizeof(GpuPointLight) * PointLightService::MAX_FRAME_POINTLIGHT;
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				bd.StructureByteStride = sizeof(GpuPointLight);
				bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

				hr = device->CreateBuffer(&bd, nullptr, &m_pointLightBuffer);

				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				srvDesc.Buffer.FirstElement = 0;
				srvDesc.Buffer.NumElements = PointLightService::MAX_FRAME_POINTLIGHT;

				hr = device->CreateShaderResourceView(m_pointLightBuffer.Get(), &srvDesc, &m_pointLightSRV);
			}

			return true;
		}
	}
}