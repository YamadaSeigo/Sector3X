#pragma once

#include "_dx11inc.h"

#include "../../Core/ECS/ServiceContext.hpp"

#include "../LightShadowService.h"

namespace SFW
{
	namespace Graphics::DX11
	{
        struct ShadowMapConfig
        {
            std::uint32_t width = 2048;
            std::uint32_t height = 2048;
            std::uint32_t cascadeCount = kMaxShadowCascades; // LightShadowService と同じ上限
            DXGI_FORMAT   texFormat = DXGI_FORMAT_R32_TYPELESS; // テクスチャ本体
            DXGI_FORMAT   dsvFormat = DXGI_FORMAT_D32_FLOAT;
            DXGI_FORMAT   srvFormat = DXGI_FORMAT_R32_FLOAT;
        };

        struct alignas(16) CBShadowCascadesData
        {
            float lightViewProj[kMaxShadowCascades][16]{}; // row-major
            float splitDepths[kMaxShadowCascades] = {};
            std::uint32_t cascadeCount = kMaxShadowCascades;
            float pad[3] = {};
        };

        /// DirectX11 用のシャドウ・リソース管理サービス
        class ShadowMapService
        {
        public:
            ShadowMapService() = default;

            bool Initialize(ID3D11Device* device, const ShadowMapConfig& cfg);

            /// 解像度変更したいとき用（必要なければ呼ばなくてOK）
            bool Resize(ID3D11Device* device, std::uint32_t width, std::uint32_t height);

			void ClearDepthBuffer(ID3D11DeviceContext* context, float clearValue = 1.0f)
			{
                for (uint32_t cascadeIdx = 0; cascadeIdx < m_cascadeCount; ++cascadeIdx)
                {
                    context->ClearDepthStencilView(
                        m_cascadeDSV[cascadeIdx].Get(),
                        D3D11_CLEAR_DEPTH,
                        clearValue,
                        0);
                }
			}

			void BindShadowPSResources(ID3D11DeviceContext* context,
                UINT shadowDataCBSlot = 0,
                UINT shadowMapSRVSlot = 0,
                UINT samplerSlot = 0) const
			{
				context->PSSetConstantBuffers(shadowDataCBSlot, 1, m_cbShadowCascades.GetAddressOf());
				context->PSSetShaderResources(shadowMapSRVSlot, 1, m_shadowSRV.GetAddressOf());
				context->PSSetSamplers(samplerSlot, 1, m_shadowSampler.GetAddressOf());
			}

			void UpdateShadowCascadeCB(ID3D11DeviceContext* context,
                const CBShadowCascadesData& data)
            {
				D3D11_MAPPED_SUBRESOURCE mapped{};
				HRESULT hr = context->Map(m_cbShadowCascades.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
				if (SUCCEEDED(hr))
				{
					std::memcpy(mapped.pData, &data, sizeof(CBShadowCascadesData));
					context->Unmap(m_cbShadowCascades.Get(), 0);
				}
			}

            void UpdateShadowCascadeCB(ID3D11DeviceContext* context,
                const LightShadowService& lightShadowService)
            {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                HRESULT hr = context->Map(m_cbShadowCascades.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                if (SUCCEEDED(hr))
                {
                    auto* dst = (CBShadowCascadesData*)mapped.pData;
					auto& cascade = lightShadowService.GetCascades();
                    std::memcpy(dst->lightViewProj, cascade.lightViewProj.data(), sizeof(dst->lightViewProj));
					std::memcpy(dst->splitDepths, lightShadowService.GetSplitDistances().data(), sizeof(float) * m_cascadeCount);
					dst->cascadeCount = m_cascadeCount;
                    context->Unmap(m_cbShadowCascades.Get(), 0);
                }
            }

            // ------------ メインパスから利用する情報 ------------

            ID3D11ShaderResourceView* GetShadowMapSRV() const noexcept
            {
                return m_shadowSRV.Get();
            }

            ID3D11SamplerState* GetShadowSampler() const noexcept
            {
                return m_shadowSampler.Get();
            }

            ID3D11RasterizerState* GetShadowRasterizerState() const noexcept
            {
                return m_shadowRS.Get();
            }

            // カスケード用ライト行列 / split が入った定数バッファ
            ID3D11Buffer* GetShadowCascadesCB() const noexcept
            {
                return m_cbShadowCascades.Get();
            }

            std::uint32_t GetCascadeCount() const noexcept { return m_cascadeCount; }

            // Terrain やメッシュのシャドウパスから DSV を直接触りたい場合用
            std::array<ComPtr<ID3D11DepthStencilView>, kMaxShadowCascades>& GetCascadeDSV() noexcept
            {
                return m_cascadeDSV;
            }

            ID3D11DepthStencilView* GetCascadeDSV(std::uint32_t i) const noexcept
            {
                return m_cascadeDSV[i].Get();
            }

            const D3D11_VIEWPORT& GetCascadeViewport(std::uint32_t i) const noexcept
            {
                return m_cascadeViewport[i];
            }

            const ShadowMapConfig& GetConfig() const noexcept { return m_config; }

        private:
            bool CreateResources(ID3D11Device* device);

            ShadowMapConfig m_config{};
            std::uint32_t m_cascadeCount = 0;

            // シャドウマップ本体 (Texture2DArray)
            ComPtr<ID3D11Texture2D> m_shadowTex;

            // 各スライスの DSV
            std::array<ComPtr<ID3D11DepthStencilView>, kMaxShadowCascades> m_cascadeDSV{};

            // 全スライスまとめて参照する SRV
            ComPtr<ID3D11ShaderResourceView> m_shadowSRV;

            // 各カスケードのビューポート
            std::array<D3D11_VIEWPORT, kMaxShadowCascades> m_cascadeViewport{};

            // 比較サンプラ（シャドウフェッチ用）
            ComPtr<ID3D11SamplerState> m_shadowSampler;

            // DepthBias 付きラスタライザ
            ComPtr<ID3D11RasterizerState> m_shadowRS;

            // シャドウカスケード情報用の定数バッファ
            ComPtr<ID3D11Buffer> m_cbShadowCascades;
        public:
            STATIC_SERVICE_TAG
        };
	}
}
