/*****************************************************************//**
 * @file   FrameTimer.h
 * @brief フレームタイマーを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once
#include <chrono>
#include <thread>

/**
 * @brief フレームタイマーを定義するクラス
 */
class FrameTimer {
public:
	using clock = std::chrono::steady_clock;
	using time_point = std::chrono::time_point<clock>;
	/**
	 * @brief コンストラクタ
	 * @detail タイマーを初期化し、開始時間を設定します。
	 */
	FrameTimer() noexcept
		: startTime(clock::now()),
		lastTime(clock::now()),
		deltaTime(0.0),
		frameCount(0),
		fps(0.0),
		timeSinceLastFPSUpdate(0.0),
		maxFrameRate(0.0) {
	}
	/**
	 * @brief タイマーをリセットします。
	 */
	void Reset() noexcept {
		startTime = clock::now();
		lastTime = startTime;
		deltaTime = 0.0;
		frameCount = 0;
		fps = 0.0;
		timeSinceLastFPSUpdate = 0.0;
	}
	/**
	 * @brief フレームタイマーを更新します。
	 */
	void Tick() {
		auto now = clock::now();
		std::chrono::duration<double> frameDuration = now - lastTime;

		// 最大FPS制限がある場合は待機
		if (maxFrameRate > 0.0) {
			const double minFrameTime = 1.0 / maxFrameRate;
			if (frameDuration.count() < minFrameTime) {
				auto sleepDuration = std::chrono::duration<double>(minFrameTime - frameDuration.count());
				std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(sleepDuration));

				now = clock::now();
				frameDuration = now - lastTime;

				// busy waitで調整（より精密に）
				while (frameDuration.count() < minFrameTime) {
					now = clock::now();
					frameDuration = now - lastTime;
				}
			}
		}

		deltaTime = frameDuration.count();
		lastTime = now;

		// FPS計測
		frameCount++;
		timeSinceLastFPSUpdate += deltaTime;

		if (timeSinceLastFPSUpdate >= 1.0) {
			fps = frameCount / timeSinceLastFPSUpdate;
			frameCount = 0;
			timeSinceLastFPSUpdate = 0.0;
		}
	}
	/**
	 * @brief デルタタイムを取得します。
	 * @return デルタタイム（秒単位）
	 */
	double GetDeltaTime() const noexcept { return deltaTime; }
	/**
	 * @brief 開始時間からの経過時間を取得します。
	 * @return 経過時間（秒単位）
	 */
	double GetTotalTime() const noexcept {
		return std::chrono::duration<double>(clock::now() - startTime).count();
	}
	/**
	 * @brief 現在のFPSを取得します。
	 * @return 現在のFPS
	 */
	double GetFPS() const noexcept { return fps; }
	/**
	 * @brief 最大フレームレートを設定します。
	 * @param fpsLimit 最大フレームレート（FPS）
	 */
	void SetMaxFrameRate(double fpsLimit) noexcept { maxFrameRate = fpsLimit; }
private:
	/**
	 * @brief 初めの開始時間
	 */
	time_point startTime;
	/**
	 * @brief 最後の更新時間
	 */
	time_point lastTime;
	/**
	 * @brief デルタタイム（前フレームからの経過時間）
	 */
	double deltaTime;
	/**
	 * @brief フレームカウント（1秒あたりのフレーム数）
	 */
	int frameCount;
	/**
	 * @brief 最後のFPS更新からの経過時間
	 */
	double timeSinceLastFPSUpdate;
	/**
	 * @brief 現在のFPS
	 */
	double fps;
	/**
	 * @brief 最大フレームレート（FPS制限）
	 */
	double maxFrameRate;
};
