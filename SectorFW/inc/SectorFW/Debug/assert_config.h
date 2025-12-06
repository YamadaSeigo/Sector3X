/*****************************************************************//**
 * @file   assert_config.h
 * @brief デバッグ用アサート設定ヘッダ
 * @author seigo_t03b63m
 * @date   December 2025
 *********************************************************************/

#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace SFW::Debug {

    inline void ReportAssertFailure(const char* expr, const char* file, int line, const char* msg = nullptr)
    {
        std::fprintf(stderr,
            "[ASSERT] %s\n  at %s(%d)\n  message: %s\n",
            expr, file, line, msg ? msg : "(none)");
        // ここでログファイルに書いたり、MessageBox 出したりも可
    }

#if defined(_MSC_VER)
#define DEBUG_BREAK() __debugbreak()
#else
#define DEBUG_BREAK() std::abort()
#endif

} // namespace Debug

//-------------------------
// デバッグ用アサート
//-------------------------
#if defined(_DEBUG) || !defined(NDEBUG)
#define SFW_ASSERT(expr)                                                     \
        do {                                                                     \
            if (!(expr)) {                                                       \
                ::SFW::Debug::ReportAssertFailure(#expr, __FILE__, __LINE__);         \
                DEBUG_BREAK();                                                   \
            }                                                                    \
        } while (0)
#else
    // Release では無効 (式も評価しない)
#define SFW_ASSERT(expr) ((void)0)
#endif

//-------------------------
// VERIFY: Release でも評価される
//-------------------------
#define SFW_VERIFY(expr)                                                         \
    do {                                                                         \
        if (!(expr)) {                                                           \
            ::SFW::Debug::ReportAssertFailure(#expr, __FILE__, __LINE__);             \
            DEBUG_BREAK();                                                       \
        }                                                                        \
    } while (0)
