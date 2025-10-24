/*****************************************************************//**
 * @file   ProcessCpuUsageWin32.h
 * @brief Win32環境でプロセスのCPU使用率を測定するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <windows.h>
#include <cstdint>

namespace SFW
{
	namespace Debug
	{
		/**
		 * @brief Win32環境でプロセスのCPU使用率を測定するクラス
		 */
		class ProcessCpuUsageWin32 {
		public:
			/**
			 * @brief CPU使用率をサンプリングする関数
			 * @return double CPU使用率 (0.0から1.0の範囲、エラー時は-1.0)
			 */
			double sample() {
				FILETIME c, e, k, u;
				if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1.0;

				const auto to64 = [](const FILETIME& ft)->uint64_t {
					return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
					};
				uint64_t kernel = to64(k), user = to64(u);

				LARGE_INTEGER now;
				QueryPerformanceCounter(&now);

				if (!init_) { lastKU_ = kernel + user; lastTS_ = now.QuadPart; init_ = true; return 0.0; }

				LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
				uint64_t kuDiff = (kernel + user) - lastKU_;
				double   sec = double(now.QuadPart - lastTS_) / double(freq.QuadPart);

				lastKU_ = kernel + user; lastTS_ = now.QuadPart;

				SYSTEM_INFO si; GetSystemInfo(&si);
				double cpuSeconds = double(kuDiff) / 10'000'000.0; // FILETIME=100ns
				double denom = sec * si.dwNumberOfProcessors;
				return (denom > 0.0) ? (cpuSeconds / denom) : 0.0; // 0..1
			}
		private:
			bool init_ = false;
			uint64_t lastKU_ = 0;
			long long lastTS_ = 0;
		};
	}
}
