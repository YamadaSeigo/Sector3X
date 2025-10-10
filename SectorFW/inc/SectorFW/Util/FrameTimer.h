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
		using namespace std::chrono;

		const bool limit = (maxFrameRate > 0.0);
		const duration<double> minFrameTime = limit ? duration<double>(1.0 / maxFrameRate)
			: duration<double>(0.0);

		auto now = clock::now();

		if (limit) {
			// 前回 tick からの理想的な次フレーム時刻を決める
			const time_point nextTick = lastTime + duration_cast<clock::duration>(minFrameTime);

			// 余裕があるなら until で寝る（丸めロスなし）
			if (now < nextTick) {
				// 大半は sleep_until で待ち、最後のわずかな差だけ軽くスピン
				// （OS 粒度が粗い環境に配慮）
				const auto spinThreshold = microseconds(200); // 調整ポイント
				const auto sleepUntilTime = nextTick - spinThreshold;
				if (now < sleepUntilTime) {
					std::this_thread::sleep_until(sleepUntilTime);
				}
				// 小さくスピン（or yield）
				do {
					std::this_thread::yield();
					now = clock::now();
				} while (now < nextTick);
			}
			else {
				// すでに遅れている場合は何もしない（追いつく）
			}
		}

		// 経過時間を計算
		const duration<double> frameDuration = now - lastTime;
		double dt = frameDuration.count();

		// スパイク対策のクランプ（必要に応じて調整）
		constexpr double MIN_DT = 0.0;
		constexpr double MAX_DT = 1.0 / 15.0; // 15 FPS 以下の巨大 dt は抑制
		if (dt < MIN_DT) dt = MIN_DT;
		if (dt > MAX_DT) dt = MAX_DT;

		deltaTime = dt;
		lastTime = now;

		// FPS 計測（移動平均の例）
		frameCount++;
		timeSinceLastFPSUpdate += deltaTime;

		// 0.25 秒ごとに更新し、EMA で滑らかに
		constexpr double UPDATE_INTERVAL = 0.25;
		if (timeSinceLastFPSUpdate >= UPDATE_INTERVAL) {
			const double instFPS = frameCount / timeSinceLastFPSUpdate;
			// 係数 0.25（お好みで）
			fps = (fps == 0.0) ? instFPS : (fps * 0.75 + instFPS * 0.25);
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
