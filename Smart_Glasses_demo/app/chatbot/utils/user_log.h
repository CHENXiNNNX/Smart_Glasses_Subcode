#ifndef USER_LOG_H
#define USER_LOG_H

#include <stdio.h>
#include <time.h>
#include <libgen.h>  // For basename()

// 获取当前时间戳
static inline void get_timestamp(char *buf, size_t len) {
    time_t raw_time;
    struct tm *time_info;

    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buf, len, "%H:%M:%S", time_info);
}

// 提取文件名（不带路径）
static inline const char* get_filename(const char *path) {
    char *filename = basename((char *)path);
    return filename ? filename : "unknown";
}

// 辅助宏，用于处理可变参数
#define USER_LOG_INTERNAL(level, format, ...) \
    do { \
        char timestamp[9]; \
        get_timestamp(timestamp, sizeof(timestamp)); \
        fprintf(stderr, "[%s] (%s) %s: " format "\n", \
                level, timestamp, __func__, ##__VA_ARGS__); \
    } while (0)

// 定义日志宏
#define USER_LOG_INFO(format, ...) USER_LOG_INTERNAL("INFO", format, ##__VA_ARGS__)
#define USER_LOG_WARN(format, ...) USER_LOG_INTERNAL("WARN", format, ##__VA_ARGS__)
#define USER_LOG_ERROR(format, ...) USER_LOG_INTERNAL("ERRO", format, ##__VA_ARGS__)

#endif // USER_LOG_H