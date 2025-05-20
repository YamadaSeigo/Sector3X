#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>

#ifdef _DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, msg) \
    assert_with_msg(expr, msg, std::source_location::current())
#else // !_DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, msg)
#endif // !_DEBUG

#ifdef _WIN32
void assert_with_msg(bool expr, const char* msg,
	const std::source_location& loc = std::source_location::current()) {
	if (!expr) {
		MessageBoxA(nullptr, msg, "Assertion Failed", MB_ICONERROR | MB_OK);
		std::abort();
	}
}
#else // !_WIN32
void assert_with_msg(bool expr, const char* msg,
	const std::source_location& loc = std::source_location::current()) {
	if (!expr) {
		std::cerr << "Assertion failed at " << loc.file_name()
			<< ":" << loc.line() << "\n"
			<< "Function: " << loc.function_name() << "\n"
			<< "Message: " << msg << std::endl;
		std::abort();
	}
}
#endif // _WIN32
