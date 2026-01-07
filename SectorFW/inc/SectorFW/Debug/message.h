#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>

namespace SFW::Debug
{

#ifdef _DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...) \
    assert_with_msg(expr , __FILE__, __LINE__, __func__, __VA_ARGS__)
#else // !_DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...)
#endif // !_DEBUG

    void assert_with_msg(bool expr, const char* file, int line, const char* func, const char* format, ...);

    void assert_with_msg(bool expr, const char* file, int line, const wchar_t* func, const char* format, ...);
}
