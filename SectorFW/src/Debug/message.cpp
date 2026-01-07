#include "Debug/message.h"
#include "Util/convert_string.h"

#ifdef _WIN32
#include <wrl/client.h>

namespace SFW::Debug
{

	inline void DebugBreakPortable() noexcept
	{
#if defined(_MSC_VER)
		__debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
		__builtin_debugtrap();        // 使えない環境もある
		// もし上が無い/効かない環境向けの保険:
		// raise(SIGTRAP);
#else
		raise(SIGTRAP);
#endif
	}

	// debugger_present.h
#pragma once

	inline bool IsDebuggerPresentPortable() noexcept
	{
#if defined(_WIN32)
#include <Windows.h>
		return ::IsDebuggerPresent();

#elif defined(__linux__)
		// Linux: /proc/self/status の TracerPid
#include <fstream>
		std::ifstream f("/proc/self/status");
		std::string line;
		while (std::getline(f, line)) {
			if (line.rfind("TracerPid:", 0) == 0) {
				int pid = std::stoi(line.substr(10));
				return pid != 0;
			}
		}
		return false;

#elif defined(__APPLE__)
		// macOS
#include <sys/types.h>
#include <sys/sysctl.h>
		int mib[4];
		struct kinfo_proc info {};
		size_t size = sizeof(info);

		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = KERN_PROC_PID;
		mib[3] = getpid();

		sysctl(mib, 4, &info, &size, nullptr, 0);
		return (info.kp_proc.p_flag & P_TRACED) != 0;

#else
		return false;
#endif
	}


	void assert_with_msg(bool expr, const char* file, int line, const char* func, const char* format, ...) {
		if (!expr) {
			constexpr size_t BUFFER_SIZE = 2048;
			char messageBuffer[BUFFER_SIZE];
			char finalBuffer[BUFFER_SIZE];

			va_list args;
			va_start(args, format);
			vsnprintf(messageBuffer, BUFFER_SIZE, format, args);
			va_end(args);

			// ファイル名・行番号・関数名を付加
			snprintf(finalBuffer, BUFFER_SIZE, "[%s:%d][%s]  %s", file, line, func, messageBuffer);

			// Retry/Ignore/Abort を出す
			int r = MessageBoxA(
				nullptr,
				finalBuffer,
				"Assertion Failed",
				MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_ABORTRETRYIGNORE
			);

			if (r == IDRETRY) {
				// デバッガがいるならここで止めて調査→(Continueで)処理継続が可能
				if (IsDebuggerPresentPortable()) {
					DebugBreakPortable();
					return; // Continueで戻ってくる
				}
				else {
					// デバッガ無しのRetryは意味が薄いので、好みで Abort と同等にする
					TerminateProcess(GetCurrentProcess(), 3);
				}
			}
			else if (r == IDIGNORE) {
				// 続行（ただしアサートを踏んでる＝状態が壊れている可能性あり）
				return;
			}

			// IDABORT or fallback
			TerminateProcess(GetCurrentProcess(), 3);
		}
	}
#else // !_WIN32
	void assert_with_msg(bool expr, const char* file, int line, const char* func, const char* format, ...) {
		if (!expr) {
			constexpr size_t BUFFER_SIZE = 2048;
			char messageBuffer[BUFFER_SIZE];
			char finalBuffer[BUFFER_SIZE];

			va_list args;
			va_start(args, format);
			vsnprintf(messageBuffer, BUFFER_SIZE, format, args);
			va_end(args);

			// ファイル名・行番号・関数名を付加
			snprintf(finalBuffer, BUFFER_SIZE, "[%s:%d][%s] %s", file, line, func, messageBuffer);

			std::cout << finalBuffer << std::endl;

			if (r == IDRETRY) {
				// デバッガがいるならここで止めて調査→(Continueで)処理継続が可能
				if (IsDebuggerPresentPortable()) {
					DebugBreakPortable();
					return; // Continueで戻ってくる
				}
				else {
					// デバッガ無しのRetryは意味が薄いので、好みで Abort と同等にする
					std::_Exit(3); // or ::_exit(3)
				}
			}
			else if (r == IDIGNORE) {
				// 続行（ただしアサートを踏んでる＝状態が壊れている可能性あり）
				return;
			}

			// IDABORT or fallback
			std::_Exit(3); // or ::_exit(3)
		}
	}
#endif // _WIN32

	void assert_with_msg(bool expr, const char* file, int line, const wchar_t* func, const char* format, ...)
	{
		std::string convertedFunc = SFW::WCharToUtf8_portable(func);
		assert_with_msg(expr, file, line, convertedFunc.c_str(), format);
	}
}
