#pragma once

#include <SectorFW/DX11Graphics.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

class DeferredRenderingService;
namespace App { struct Context; }

namespace RenderPipe
{
    void Initialize(
        SFW::Graphics::DX11::GraphicsDevice::RenderGraph* renderGraph,
        App::Context& ctx,
        ComPtr<ID3D11RenderTargetView>& mainRTV,
        ComPtr<ID3D11DepthStencilView>& mainDSV,
        ComPtr<ID3D11DepthStencilView>& mainDSVReadOnly,
        ComPtr<ID3D11ShaderResourceView>& mainDepthSRV,
        SFW::Graphics::PassCustomFuncType drawTerrainColor,
        SFW::Graphics::PassCustomFuncType drawParticle
    );
}
