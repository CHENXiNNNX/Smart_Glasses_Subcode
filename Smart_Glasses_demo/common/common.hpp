#pragma once

#include <ctime>

#if __has_include("sample_comm.h")
#include "sample_comm.h"
#endif

#if !__has_include("sample_comm.h")
using RK_U64 = unsigned long long;
#endif

constexpr RK_U64 MICROSECONDS_PER_SECOND     = 1000000ULL;
constexpr RK_U64 NANOSECONDS_PER_MICROSECOND = 1000ULL;

// 统一时间管理
inline RK_U64 get_nowus() // NOLINT(readability-identifier-naming)
{
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<RK_U64>(time.tv_sec) * MICROSECONDS_PER_SECOND +
           static_cast<RK_U64>(time.tv_nsec) / NANOSECONDS_PER_MICROSECOND;
}
