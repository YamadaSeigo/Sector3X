#include "message.h"

#ifdef _WIN32
#include <wrl/client.h>

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

		MessageBoxA(nullptr, finalBuffer, "Assertion Failed", MB_ICONERROR | MB_OK);
		std::abort();
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
		std::abort();
	}
}
#endif // _WIN32