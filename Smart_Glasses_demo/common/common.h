#pragma once

#include "../rkmpi/include/sample_comm.h"

// 统一时间管理
inline RK_U64 get_nowus(void) {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000;
}


