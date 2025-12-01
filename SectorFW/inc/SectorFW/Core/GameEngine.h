/*****************************************************************//**
 * @file   GameEngine.h
 * @brief  ゲームロジックを実行するクラス
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#include "../Graphics/IGraphicsDevice.hpp"

#include "../Util/FrameTimer.h"
#include "../Util/NonCopyable.h"

#include "World.hpp"
#include "../Util/TypeChecker.hpp"

namespace SFW
{
	/**
	 * @brief Graphics::IGraphicsDeviceをCRTPで継承している型をチェックするコンセプト
	 */
	template<typename T>
	concept GraphicsType = is_crtp_base_of_1arg<T, Graphics::IGraphicsDevice>;

	/**
	 * @brief ゲームエンジンの初期化
	 */
	void InitializeGameEngine(bool initialize);
	/**
	 * @brief ゲームエンジンの終了処理
	 */
	void UnInitializeGameEngine();

	/**
	 * @brief ゲームエンジン
	 * @class GameEngine
	 * @detailss ゲームロジックを実行するクラス
	 */
	template<GraphicsType Graphics, typename... LevelTypes>
	class GameEngine final : NonCopyable
	{
		using WorldType = World<LevelTypes...>;

	public:
		/**
		 * @brief コンストラクタ
		 * @param fps フレームレート
		 * @param graphicsDevice グラフィックデバイス
		 * @detailss FPS制御クラスを初期化し、グラフィックデバイスを設定します。
		 */
		explicit GameEngine(Graphics&& graphicsDevice, WorldType&& world, double fps = 60.0)
			: m_graphicsDevice(std::move(graphicsDevice)), m_world(std::move(world))
		{
			InitializeGameEngine(m_graphicsDevice.IsInitialized());

			m_frameTimer.SetMaxFrameRate(fps);
		}
		/**
		 * @brief デストラクタ
		 */
		~GameEngine()
		{
			UnInitializeGameEngine();
		}

		/**
		 * @brief メインループ
		 * @param fpsControl FPS制御クラスのインスタンス
		 * @detailss FPS制御クラスを使用して、メインループを実行します。
		 */
		void MainLoop(IThreadExecutor* executor)
		{
			// 更新処理、描画処理を呼び出す
			Update(m_frameTimer.GetDeltaTime(), executor);
			Draw();

			//経過時間を計算と待機
			m_frameTimer.Tick();
		}
	private:
		/**
		 * @brief 更新処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @detailss 更新処理を実行します。
		 */
		void Update(double delta_time, IThreadExecutor* executor)
		{
			m_world.UpdateServiceLocator(delta_time, executor);

			m_world.UpdateAllLevels(delta_time, executor);
		}
		/**
		 * @brief 描画処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @detailss 描画処理を実行します。
		 */
		void Draw()
		{
			// 画面をクリア
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_graphicsDevice.SubmitFrame(clearColor, m_frameCounter++); // 非同期提出
			// 計測やスクショ等で必要なら: m_graphicsDevice.WaitSubmittedFrames(m_frameCounter - 1);
		}
	private:
		// FPS制御クラス
		FrameTimer m_frameTimer;
		// グラフィックデバイス
		Graphics m_graphicsDevice;
		//ワールド管理
		WorldType m_world;
		// フレームカウンタ
		uint64_t m_frameCounter = 0;
	};
}