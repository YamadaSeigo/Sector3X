/*****************************************************************//**
 * @file   logger.hpp
 * @brief  ログ出力のためのヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#ifdef _DEBUG

#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

#include <cstdarg>
#include <vector>

#include <mutex>

#endif //_DEBUG

 // ログにファイル名、行番号、関数名を表示する場合は以下のいずれかを有効化
#define LOG_SHOW_FILE
#define LOG_SHOW_LINE
//#define LOG_SHOW_FUNC

// ログに詳細なタイムスタンプ（ミリ秒まで）を表示する場合は以下を有効化
//#define LOG_SHOW_DETAIL_TIME

namespace SectorFW {
	namespace Debug {
#ifdef _DEBUG

#if defined(_WIN32)
#include <windows.h>

		/**
		 * @brief コンソールの色を設定する関数
		 * @param level ログレベル（0: 情報, 1: 警告, 2: エラー）
		 */
		inline void SetConsoleColor(int level) {
			HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
			WORD color;
			switch (level) {
			case 0: color = 7; break; // Info
			case 1: color = FOREGROUND_RED | FOREGROUND_GREEN; break; // Warning
			case 2: color = FOREGROUND_RED | FOREGROUND_INTENSITY; break; // Error
			default: color = 7; break;
			}
			SetConsoleTextAttribute(hConsole, color);
		}
#else // LinuxやMacOSなどの他のプラットフォームではANSIエスケープシーケンスを使用
		/**
		 * @brief コンソールの色を設定する関数
		 * @param level ログレベル（0: 情報, 1: 警告, 2: エラー）
		 */
		inline void SetConsoleColor(int level) {
			switch (level) {
			case 0: std::cout << "\033[0m"; break;        // Info
			case 1: std::cout << "\033[33m"; break;       // Warning
			case 2: std::cout << "\033[31m"; break;       // Error
			default: std::cout << "\033[0m"; break;
			}
		}
#endif // _WIN32
		/**
		 * @brief 安全にローカル時間を取得する関数
		 * @param time タイムスタンプ（std::time_t型）
		 * @return std::tm型のローカル時間構造体
		 */
		inline std::tm SafeLocalTime(std::time_t time) {
			std::tm tm_result;
#if defined(_WIN32)
			localtime_s(&tm_result, &time);
#else
			localtime_r(&time, &tm_result);
#endif
			return tm_result;
		}

		/**
		 * @brief 現在のタイムスタンプを取得する関数
		 * @return 現在のタイムスタンプ文字列（例: "2023-10-01 12:34:56.789"）
		 */
		inline std::string GetCurrentTimestamp() {
			auto now = std::chrono::system_clock::now();
			auto time = std::chrono::system_clock::to_time_t(now);
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				now.time_since_epoch()) % 1000;

			std::ostringstream oss;
			std::tm tm = SafeLocalTime(time);
			oss
#ifdef LOG_SHOW_DETAIL_TIME
				<< std::put_time(&tm, "%m-%d %H:%M:%S")
				<< "." << std::setfill('0') << std::setw(3) << ms.count();
#else
				<< std::put_time(&tm, "%H:%M:%S");
#endif

			return oss.str();
		}

		/**
		 * @brief ログ出力用のミューテックスを取得する関数
		 * @return ログ出力用のミューテックス
		 */
		inline std::mutex& GetLogMutex() {
			static std::mutex mtx;
			return mtx;
		}
		/**
		 * @brief   ログ出力関数
		 * @param level ログレベル（0: 情報, 1: 警告, 2: エラー）
		 * @param prefix ログのプレフィックス（例: "Info", "Warning", "Error"）
		 * @param file ログ出力元のファイル名
		 * @param line ログ出力元の行番号
		 * @param function ログ出力元の関数名
		 * @param msg ログメッセージ
		 */
		inline void LogImpl(int level, const char* prefix, const char* file, int line, const char* function, const std::string& msg) {
			std::lock_guard<std::mutex> lock(GetLogMutex()); // ← スレッドセーフ！

			SetConsoleColor(level);

			std::cout
				<< "[" << GetCurrentTimestamp() << "] "
				<< "[" << prefix << "] "
#if defined(LOG_SHOW_FILE) || defined(LOG_SHOW_LINE) || defined(LOG_SHOW_FUNC)
				<< "["
#ifdef LOG_SHOW_FILE
				<< file
#endif
#ifdef LOG_SHOW_LINE
				<< ":" << line
#endif
#ifdef LOG_SHOW_FUNC
				<< " " << function
#endif
				<< "] "
#endif
				<< msg << std::endl;

#if defined(_WIN32)
			SetConsoleColor(0);
#else // LinuxやMacOSなどの他のプラットフォームでは、デフォルトの色に戻す
			std::cout << "\033[0m";
#endif // _WIN32
		}

		/**
		 * @brief フォーマットされた文字列を生成する関数
		 * @param fmt フォーマット文字列（printfスタイル）
		 * @param ... 可変長引数（フォーマットに使用される値）
		 * @return フォーマットされた文字列
		 */
		inline std::string FormatPrintf(const char* fmt, ...) {
			va_list args1;
			va_start(args1, fmt);
			va_list args2;
			va_copy(args2, args1);
			const int len = std::vsnprintf(nullptr, 0, fmt, args1);
			va_end(args1);

			std::vector<char> buffer(len + 1);
			std::vsnprintf(buffer.data(), buffer.size(), fmt, args2);
			va_end(args2);

			return std::string(buffer.data(), len);
		}
#endif // _DEBUG
	}

	// デバッグビルド時のみログ出力
#ifdef _DEBUG
// ログ出力マクロ
#define LOG_INFO(fmt, ...)    SectorFW::Debug::LogImpl(0, "Info",    __FILE__, __LINE__, __FUNCTION__, SectorFW::Debug::FormatPrintf(fmt, ##__VA_ARGS__))
// 警告ログ出力マクロ
#define LOG_WARNING(fmt, ...) SectorFW::Debug::LogImpl(1, "Warning", __FILE__, __LINE__, __FUNCTION__, SectorFW::Debug::FormatPrintf(fmt, ##__VA_ARGS__))
// エラーログ出力マクロ
#define LOG_ERROR(fmt, ...)   SectorFW::Debug::LogImpl(2, "Error",   __FILE__, __LINE__, __FUNCTION__, SectorFW::Debug::FormatPrintf(fmt, ##__VA_ARGS__))

#else // DEBUG

	// リリースビルドでは空マクロ
#define LOG_INFO(fmt, ...)    ((void)0)
#define LOG_WARNING(fmt, ...) ((void)0)
#define LOG_ERROR(fmt, ...)   ((void)0)

#endif //! DEBUG
}
