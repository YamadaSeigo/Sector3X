/*****************************************************************//**
 * @file   GameEngine.h
 * @brief  ゲームロジックを実行するクラス
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#include "Util/FpsControl.h"
#include "Util/NonCopyable.h"

namespace SectorFW
{
	/**
	 * @brief ゲームエンジン
	 * @class GameEngine
	 * @details ゲームロジックを実行するクラス
	 */
	class GameEngine final : NonCopyable
	{
	public:
		/**
		 * @brief コンストラクタ
		 * @param fps フレームレート
		 * @param graphicsDevice グラフィックデバイス
		 * @details FPS制御クラスを初期化し、グラフィックデバイスを設定します。
		 */
		explicit GameEngine(uint64_t fps, std::unique_ptr<IGraphicsDevice>&& graphicsDevice);
		/**
		 * @brief デストラクタ
		 */
		~GameEngine() = default;

		/**
		 * @brief メインループ
		 * @param fpsControl FPS制御クラスのインスタンス
		 * @details FPS制御クラスを使用して、メインループを実行します。
		 */
		void MainLoop();
	private:
		/**
		 * @brief 更新処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @details 更新処理を実行します。
		 */
		void Update(uint64_t delta_time);
		/**
		 * @brief 描画処理
		 * @param delta_time 前回実行されてからの経過時間
		 * @details 描画処理を実行します。
		 */
		void Draw(uint64_t delta_time);
	private:
		// FPS制御クラス
		FPS m_fpsControl;
		// グラフィックデバイス
		std::unique_ptr<IGraphicsDevice> m_graphicsDevice;
	};
}