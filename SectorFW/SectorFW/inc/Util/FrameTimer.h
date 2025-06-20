#pragma once
#include <chrono>
#include <thread>

class FrameTimer {
public:
	using clock = std::chrono::steady_clock;
	using time_point = std::chrono::time_point<clock>;

	FrameTimer()
		: startTime(clock::now()),
		lastTime(clock::now()),
		deltaTime(0.0),
		frameCount(0),
		fps(0.0),
		timeSinceLastFPSUpdate(0.0),
		maxFrameRate(0.0) {
	}

	void Reset() {
		startTime = clock::now();
		lastTime = startTime;
		deltaTime = 0.0;
		frameCount = 0;
		fps = 0.0;
		timeSinceLastFPSUpdate = 0.0;
	}

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

	double GetDeltaTime() const { return deltaTime; }
	double GetTotalTime() const {
		return std::chrono::duration<double>(clock::now() - startTime).count();
	}

	double GetFPS() const { return fps; }
	void SetMaxFrameRate(double fpsLimit) { maxFrameRate = fpsLimit; }

private:
	time_point startTime;
	time_point lastTime;
	double deltaTime;

	int frameCount;
	double timeSinceLastFPSUpdate;
	double fps;
	double maxFrameRate;
};
