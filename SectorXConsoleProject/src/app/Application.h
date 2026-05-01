#pragma once

#include <memory>

#include <wrl/client.h>
#include <d3d11.h>

#include "app/AppContext.h"
#include "terrain/TerrainWater.h"
#include "terrain/TerrainBootstrap.h"

#include "../graphics/SpriteAnimationService.h"
#include <SectorFW/Graphics/PointLightService.h>
#include "PlayerService.h"

namespace SFW
{
    class SimpleThreadPool;
    class TimerService;

    namespace Graphics::DX11
    {
        class GraphicsDevice;
    }

    namespace Physics
    {
        class PhysicsDevice;
        class PhysicsShapeManager;
        class PhysicsService;
    }

    namespace Input
    {
        class WinInput;
    }

    namespace Graphics
    {
        class LightShadowService;
        class PointLightService;
        struct HeightTexMapping;

        namespace DX11
        {
            class PerCamera3DService;
            class OrtCamera3DService;
            class Camera2DService;
            class LightShadowResourceService;
        }
    }

    namespace Audio
    {
        class AudioService;
    }
}

class WindService;
class PlayerService;
class DeferredRenderingService;
class EnvironmentService;
class SpriteAnimationService;
class FireflyService;
class LeafService;
class RainService;

namespace App
{
    

    class Application
    {
    public:
        bool Initialize();
        void Run();
        int Shutdown();

    private:
        bool InitializeWindow();
        bool InitializeGraphics();
        bool InitializeTerrain();
        bool InitializeWater();
        bool InitializePhysics();
        bool InitializeServices();
        bool InitializeParticleResources();
        bool InitializeContext();
        bool InitializeRenderPipeline();
        bool InitializeGameEngine();

        void RegisterRenderCallbacks();
        void RegisterDebugUI();
        void LoadInitialLevel();

    private:
        static void DrawTerrainColor(uint64_t frame);
        static void DrawOpaqueParticle(uint64_t frame);
        static void DrawTransparentParticle(uint64_t frame);

    private:
        static Context ctx_;

        static std::unique_ptr<SFW::SimpleThreadPool> threadPool_;

        static GraphicsDeviceType graphics_;

        static TerrainBoot::Result terrainRes_;
        static TerrainWater terrainWater_;

        Physics::PhysicsDevice physics_;
        Physics::PhysicsShapeManager shapeManager_;
        std::unique_ptr<Physics::PhysicsService> physicsService_;

        std::unique_ptr<InputService> winInput_;

        static std::unique_ptr<Graphics::I3DPerCameraService> perCameraService_;
        std::unique_ptr<Graphics::I3DOrtCameraService> ortCameraService_;
        std::unique_ptr<Graphics::I2DCameraService> camera2DService_;

        static std::unique_ptr<Graphics::LightShadowService> lightShadowService_;
        static std::unique_ptr<WindService> windService_;
        std::unique_ptr<PlayerService> playerService_;
        std::unique_ptr<Audio::AudioService> audioService_;
        static std::unique_ptr<DeferredRenderingService> deferredRenderingService_;
        static std::unique_ptr<Graphics::DX11::LightShadowResourceService> lightShadowResourceService_;
        std::unique_ptr<Graphics::PointLightService> pointLightService_;
        static std::unique_ptr<EnvironmentService> environmentService_;
        std::unique_ptr<SpriteAnimationService> spriteAnimationService_;
        static std::unique_ptr<FireflyService> fireflyService_;
        static std::unique_ptr<LeafService> leafService_;
        static std::unique_ptr<RainService> rainService_;
        static std::unique_ptr<SFW::TimerService> timerService_;

        std::unique_ptr<ECS::ServiceLocator> serviceLocator_;
        static std::unique_ptr<GameEngineType> gameEngine_;

        static ComPtr<ID3D11SamplerState> linearSampler_;
        static ComPtr<ID3D11SamplerState> pointSampler_;
        static ComPtr<ID3D11ShaderResourceView> leafTextureSRV_;

        static Graphics::HeightTexMapping heightTexMap_;

        int terrainRank_ = 4;
    };
}