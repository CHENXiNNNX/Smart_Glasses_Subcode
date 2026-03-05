/*
 * time.hpp - 时间工具
 */

#pragma once

#include <cstdint>
#include <string>

namespace app::tool::time
{

    /* Unix时间戳 */
    int64_t unix_timestamp_sec(); // 秒
    int64_t unix_timestamp_ms();  // 毫秒
    int64_t unix_timestamp_us();  // 微秒

    /* 系统运行时间 */
    int64_t uptime_ms();  // 毫秒
    int64_t uptime_us();  // 微秒
    int64_t uptime_sec(); // 秒

    /* 时间字符串 */
    std::string iso8601_timestamp(); // UTC格式
    std::string local_time_string(); // 本地时间

} // namespace app::tool::time
