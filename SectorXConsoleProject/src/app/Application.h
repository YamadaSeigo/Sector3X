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

//前方宣言
//=========================================================================
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
//=========================================================================

namespace App
{
    
    /**
	 * @brief アプリケーションを管理するクラス(いわばゲーム全体のコントローラー、神クラス)
     */
    class Application
    {
    public:
        /**
		 * @brief アプリケーションの初期化。アプリ全体で一度しか呼ばれないことを想定している
		 * @return 初期化が成功したかどうか
         */
        bool Initialize();
        /**
		 * @brief ゲームループを回す。Initialize()が成功した後に呼び出されることを想定している
         */
        void Run();
        /**
		 * @brief アプリケーションのシャットダウン。リソースの解放などを行う。Run()が終了した後に呼び出されることを想定している
		 * @return ウィンドウを削除した後の終了コード。main関数のreturn値として使われることを想定している
         */
        int Shutdown();

    private:
        /**
		 * @brief ウィンドウの初期化
         * @return 初期化が成功したかどうか
         */
        bool InitializeWindow();
        /**
		 * @brief グラフィックスの初期化
         * @return 初期化が成功したかどうか
         */
        bool InitializeGraphics();
        /**
         * @brief 地形の生成
		 * @return 生成が成功したかどうか
         */
        bool InitializeTerrain();
        /**
         * @brief 水面地形の初期化(実験中)
		 * @return　初期化が成功したかどうか
         */
        bool InitializeWater();
        /**
		 * @brief 物理デバイスの初期化
		 * @return 初期化が成功したかどうか
         */
        bool InitializePhysics();
        /**
		 * @brief 使用するすべてのサービスを初期化
		 * @return 初期化が成功したかどうか
         */
        bool InitializeServices();
        /**
		 * @brief パーティクル関連のリソースを初期化する
		 * @return 初期化が成功したかどうか
         */
        bool InitializeParticleResources();
        /**
		 * @brief 他の関数に渡すためのコンテキストを初期化する
		 * @return 初期化が成功したかどうか
         */
        bool InitializeContext();
        /**
		 * @brief 描画パイプラインを構築する
		 * @return　初期化が成功したかどうか
         */
        bool InitializeRenderPipeline();
        /**
		 * @brief ゲームループをまわすためのゲームエンジンを初期化する
		 * @return 初期化が成功したかどうか
         */
        bool InitializeGameEngine();
        /**
        * @brief 描画デバイスでカスタムレンダリングを行うためのコールバックを登録する
        * @return 初期化が成功したかどうか
        */
        void RegisterRenderCallbacks();
        /**
		 * @brief デバッグUIを登録する
         */
        void RegisterDebugUI();
        /**
		 * @brief 最初のレベルを読み込む
         */
        void LoadInitialLevel();

    private:
		//地形をカラー描画する関数。RenderGraph のカスタムパスとして登録する予定
        static void DrawTerrainColor(uint64_t frame);
		//不透明のパーティクルを描画する関数。RenderGraph のカスタムパスとして登録する予定
        static void DrawOpaqueParticle(uint64_t frame);
		//半透明のパーティクルを描画する関数。RenderGraph のカスタムパスとして登録する予定
        static void DrawTransparentParticle(uint64_t frame);

    private:

		//すこしでも処理を軽くする目的で、ラムダのキャプチャを減らすために、必要なものは静的メンバにしておく

        static Context m_ctx;

        static std::unique_ptr<SFW::SimpleThreadPool> m_threadPool;

        static GraphicsDeviceType m_graphics;

        static TerrainBoot::Result m_terrainRes;
        static TerrainWater m_terrainWater;

        Physics::PhysicsDevice m_physics;
        Physics::PhysicsShapeManager m_shapeManager;
        std::unique_ptr<Physics::PhysicsService> m_physicsService;

        std::unique_ptr<InputService> m_winInput;

        static std::unique_ptr<Graphics::I3DPerCameraService> m_perCameraService;
        std::unique_ptr<Graphics::I3DOrtCameraService> m_ortCameraService;
        std::unique_ptr<Graphics::I2DCameraService> m_camera2DService;

        static std::unique_ptr<Graphics::LightShadowService> m_lightShadowService;
        static std::unique_ptr<WindService> m_windService;
        std::unique_ptr<PlayerService> m_playerService;
        std::unique_ptr<Audio::AudioService> m_audioService;
        static std::unique_ptr<DeferredRenderingService> m_deferredRenderingService;
        static std::unique_ptr<Graphics::DX11::LightShadowResourceService> m_lightShadowResourceService;
        std::unique_ptr<Graphics::PointLightService> m_pointLightService;
        static std::unique_ptr<EnvironmentService> m_environmentService;
        std::unique_ptr<SpriteAnimationService> m_spriteAnimationService;
        static std::unique_ptr<FireflyService> m_fireflyService;
        static std::unique_ptr<LeafService> m_leafService;
        static std::unique_ptr<RainService> m_rainService;
        static std::unique_ptr<SFW::TimerService> m_timerService;
        std::unique_ptr<ECS::ServiceLocator> m_serviceLocator;
        static std::unique_ptr<GameEngineType> m_gameEngine;

        static ComPtr<ID3D11SamplerState> m_linearSampler;
        static ComPtr<ID3D11SamplerState> m_pointSampler;
        static ComPtr<ID3D11ShaderResourceView> m_leafTextureSRV;

        static Graphics::HeightTexMapping m_heightTexMap;

        int m_terrainRank = 4;
    };
}