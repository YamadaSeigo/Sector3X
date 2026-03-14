/*****************************************************************//**
 * \file   message.h
 * \brief 動的なアサーションとメッセージ出力のためのヘッダーファイル
 * \author seigo
 * \date   December 2025
 *********************************************************************/

#pragma once

#include <iostream>
#include <cstdlib>
#include <source_location>

namespace SFW::Debug
{
	void assert_with_msg(bool expr, const char* file, int line, const char* func, const char* format, ...);

	void assert_with_msg(bool expr, const char* file, int line, const wchar_t* func, const char* format, ...);
}

// 動的なアサーションマクロ。デバッグビルドでのみ有効で、条件が偽の場合にファイル名、行番号、関数名、およびカスタムメッセージを出力します。

#ifdef _DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...) \
    SFW::Debug::assert_with_msg(expr , __FILE__, __LINE__, __func__, __VA_ARGS__)
#else // !_DEBUG
#define DYNAMIC_ASSERT_MESSAGE(expr, ...) ((void)0)
#endif // !_DEBUG