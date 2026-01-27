#pragma once
#include <atomic>
#include <memory>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// forward declarations（必要なものだけ include）
namespace SFW::Graphics::DX11 {
    class GraphicsDevice;
    class LightShadowResourceService;
}
namespace SFW::Graphics {
    struct RenderService;
}
class DeferredRenderingService;
class WindService;
class PlayerService;
class EnvironmentService;
class FireflyService;
class LeafService;

namespace App
{
    struct Context
    {
        // 実行フラグ
        std::atomic<bool> executeCustom{ false };

        // graphics / render
        SFW::Graphics::DX11::GraphicsDevice* graphics = nullptr;
        SFW::Graphics::RenderService* renderService = nullptr;

        // services
        SFW::Graphics::DX11::LightShadowResourceService* shadowRes = nullptr;
        DeferredRenderingService* deferred = nullptr;
        WindService* wind = nullptr;
        PlayerService* player = nullptr;
        EnvironmentService* env = nullptr;
        FireflyService* firefly = nullptr;
        LeafService* leaf = nullptr;

        // 共有する D3D SRV/CB など（必要なら追加）
        ComPtr<ID3D11ShaderResourceView> mainDepthSRV;
    };
}
