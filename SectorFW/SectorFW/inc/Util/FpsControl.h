/*****************************************************************//**
 * @file   FpsControl.h
 * @brief  FPS制御クラス
 * @author suzuki
 * @date   May 2025
 *********************************************************************/

#pragma once

#include <chrono>
#include <thread>
#include <iostream>
#include <cstdint>

 /**
  * @brief FPS制御クラス
  * @note フレームレートを制御するためのクラスです。
  *       FPSを指定して、デルタタイムを計算し、必要に応じてスリープします。
  */
class FPS {
public:
	/**
	 * @brief コンストラクタ削除
	 */
	FPS() = delete;

	/**
	 * @brief コンストラクタ(暗黙的変換防止)
	 * @param fps フレームレート
	 */
	explicit FPS(uint64_t fps)
		: m_MicrosecondsPerFrame(1000000 / fps),
		m_last_time(std::chrono::steady_clock::now())
	{
	}

	/**
	 * @brief デルタタイムの計算
	 * @return　デルタタイム(マイクロ秒)
	 */
	uint64_t CalcDelta() {
		auto now = std::chrono::steady_clock::now();
		auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_time).count();
		m_last_time = now;
		m_delta_time = delta_us;
		return m_delta_time;
	}

	/**
	 * @brief FPS制御のための待機
	 * @note フレームレートを維持するために、必要な時間だけスリープします。
	 */
	void Wait() const {
		int64_t sleep_us = static_cast<int64_t>(m_MicrosecondsPerFrame) - static_cast<int64_t>(m_delta_time);
		if (sleep_us > 0) {
			std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
		}
	}

private:
	// フレームレート(マイクロ秒)
	uint64_t m_MicrosecondsPerFrame = 0;
	// フレームレート(マイクロ秒)
	uint64_t m_delta_time = 0;
	// 前回のフレームの時間
	std::chrono::steady_clock::time_point m_last_time;
};
