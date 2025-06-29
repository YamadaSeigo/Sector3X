/*****************************************************************//**
 * @file   GameEngine.h
 * @brief  ゲームロジックを実行するクラス
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#include "Util/FrameTimer.h"
#include "Util/NonCopyable.h"

#include "World.hpp"
#include "Util/TypeChecker.hpp"

namespace SectorFW
{
	template<typename T>
	concept GraphicsType = (is_crtp_base_of<T, IGraphicsDevice>::value);

	void InitializeGameEngine(bool initialize);

	void UnInitializeGameEngine();

	/**
	 * @brief ゲームエンジン
	 * @class GameEngine
	 * @details ゲームロジックを実行するクラス
	 */
	template<GraphicsType T, typename... LevelTypes>
	class GameEngine final : NonCopyable
	{
		using WorldType = World<LevelTypes...>;

	public:
		/**
		 * @brief コンストラクタ
		 * @param fps フレームレート
		 * @param graphicsDevice グラフィックデバイス
		 * @details FPS制御クラスを初期化し、グラフィックデバイスを設定します。
		 */
		explicit GameEngine(std::unique_ptr<T>&& graphicsDevice, WorldType&& world, double fps = 60.0)
			: m_graphicsDevice(std::move(graphicsDevice)), m_world(std::move(world))
		{
			InitializeGameEngine(m_graphicsDevice->IsInitialized());

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
		 * @details FPS制御クラスを使用して、メインループを実行します。
		 */
		void MainLoop()
		{
			// 前回実行されてからの経過時間を取得
			double delta_time = m_frameTimer.GetDeltaTime();

			// 更新処理、描画処理を呼び出す
			Update(delta_time);
			Draw(delta_time);

			//経過時間を計算と待機
			m_frameTimer.Tick();
		}
	private:
		/**
		 * @brief 更新処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @details 更新処理を実行します。
		 */
		void Update(double delta_time)
		{
			m_world.UpdateAllLevels();
		}
		/**
		 * @brief 描画処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @details 描画処理を実行します。
		 */
		void Draw(double delta_time)
		{
			// 画面をクリア
			float clearColor[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			m_graphicsDevice->Clear(clearColor);

			// 描画処理
			m_graphicsDevice->Present();
		}
	private:
		// FPS制御クラス
		FrameTimer m_frameTimer;
		// グラフィックデバイス
		std::unique_ptr<T> m_graphicsDevice;
		//ワールド管理
		WorldType m_world;
	};
}