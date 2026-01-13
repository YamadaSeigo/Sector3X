#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>

namespace SFW::Debug
{

    void assert_with_msg(bool expr, const char* file, int line, const char* func, const char* format, ...);

    void assert_with_msg(bool expr, const char* file, int line, const wchar_t* func, const char* format, ...);
}

#ifdef _DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...) \
    SFW::Debug::assert_with_msg(expr , __FILE__, __LINE__, __func__, __VA_ARGS__)
#else // !_DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...) ((void)0)
#endif // !_DEBUG