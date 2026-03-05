/*
 * time.cc - 时间工具
 */

#include "time.hpp"

#include <ctime>
#include <sys/time.h>

namespace app::tool::time
{

    int64_t unix_timestamp_sec()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return static_cast<int64_t>(tv.tv_sec);
    }

    int64_t unix_timestamp_ms()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return (static_cast<int64_t>(tv.tv_sec) * 1000LL) +
               (static_cast<int64_t>(tv.tv_usec) / 1000LL);
    }

    int64_t unix_timestamp_us()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return (static_cast<int64_t>(tv.tv_sec) * 1000000LL) + static_cast<int64_t>(tv.tv_usec);
    }

    int64_t uptime_ms()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<int64_t>(ts.tv_sec) * 1000LL) +
               (static_cast<int64_t>(ts.tv_nsec) / 1000000LL);
    }

    int64_t uptime_us()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<int64_t>(ts.tv_sec) * 1000000LL) +
               (static_cast<int64_t>(ts.tv_nsec) / 1000LL);
    }

    int64_t uptime_sec()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<int64_t>(ts.tv_sec);
    }

    std::string iso8601_timestamp()
    {
        int64_t   timestamp_sec = unix_timestamp_sec();
        struct tm tm_info;
        time_t    time_val = static_cast<time_t>(timestamp_sec);
        gmtime_r(&time_val, &tm_info);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
        return std::string(buffer);
    }

    std::string local_time_string()
    {
        int64_t   timestamp_sec = unix_timestamp_sec();
        struct tm tm_info;
        time_t    time_val = static_cast<time_t>(timestamp_sec);
        localtime_r(&time_val, &tm_info);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
        return std::string(buffer);
    }

} // namespace app::tool::time
