/*
 * log.hpp - 异步日志管理
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#define DEFAULT_MAX_FILE_SIZE_MB 5UL
#define DEFAULT_BUFFER_SIZE 8192UL
#define BYTES_PER_KB 1024UL
#define BYTES_PER_MB (1024UL * 1024UL)

namespace app::tool::log
{

    /* 日志级别 */
    enum class LogLevel
    {
        DEBUG = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3
    };

    /* 日志配置 */
    struct LogConfig
    {
        bool        enable_console;
        bool        enable_file;
        bool        enable_color;
        bool        enable_timestamp;
        bool        enable_thread_id;
        std::string log_file_path;
        size_t      max_file_size;
        size_t      buffer_size;
        LogLevel    min_level;

        LogConfig()
            : enable_console(true), enable_file(true), enable_color(true), enable_timestamp(false),
              enable_thread_id(false), log_file_path("./log/smart_glasses.log"),
              max_file_size(DEFAULT_MAX_FILE_SIZE_MB * BYTES_PER_MB),
              buffer_size(DEFAULT_BUFFER_SIZE), min_level(LogLevel::DEBUG)
        {
        }
    };

    /* 日志消息 */
    struct LogMessage
    {
        LogLevel        level;
        uint64_t        timestamp;
        std::string     tag;
        std::string     message;
        std::thread::id thread_id;

        LogMessage() : level(LogLevel::INFO), timestamp(0), thread_id(std::this_thread::get_id()) {}
    };

    /* 日志管理器 (单例) */
    class Logger
    {
    public:
        static Logger& inst();

        bool init(const LogConfig& config = LogConfig());
        bool deinit();

        void log(LogLevel level, const char* tag, const char* format, ...);
        void log(LogLevel level, const char* tag, const std::string& message);

        void set_min_level(LogLevel level);
        void set_console_enabled(bool enabled);
        void set_file_enabled(bool enabled);
        void set_color_enabled(bool enabled);
        void set_timestamp_enabled(bool enabled);
        void set_thread_id_enabled(bool enabled);
        void flush();
        bool is_running() const;

        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        Logger();
        ~Logger();

        void            worker_thread();
        void            write_to_console(const LogMessage& msg);
        void            write_to_file(const LogMessage& msg);
        std::string     format_message(const LogMessage& msg, bool with_color);
        void            rotate_file_if_needed();
        const char*     get_level_string(LogLevel level);
        const char*     get_level_color(LogLevel level);
        static uint64_t get_current_timestamp();

    private:
        LogConfig                     config_;
        std::atomic<bool>             running_;
        std::atomic<bool>             initialized_;
        std::unique_ptr<LogMessage[]> buffer_;
        std::atomic<size_t>           write_index_;
        std::atomic<size_t>           read_index_;
        std::atomic<size_t>           message_count_;
        std::thread                   worker_thread_;
        std::mutex                    mutex_;
        std::condition_variable       cv_;
        FILE*                         log_file_;
        std::atomic<size_t>           current_file_size_;
        std::mutex                    file_mutex_;
    };

    /*----------------------------------------------------------------------------
     * 日志宏
     *----------------------------------------------------------------------------*/

#define USER_LOG(level, tag, format, ...)                                                          \
    do                                                                                             \
    {                                                                                              \
        app::tool::log::LogLevel log_level;                                                        \
        if (strcmp(level, "DEBUG") == 0)                                                           \
            log_level = app::tool::log::LogLevel::DEBUG;                                           \
        else if (strcmp(level, "INFO") == 0)                                                       \
            log_level = app::tool::log::LogLevel::INFO;                                            \
        else if (strcmp(level, "WARN") == 0)                                                       \
            log_level = app::tool::log::LogLevel::WARN;                                            \
        else if (strcmp(level, "ERROR") == 0)                                                      \
            log_level = app::tool::log::LogLevel::ERROR;                                           \
        else                                                                                       \
            log_level = app::tool::log::LogLevel::INFO;                                            \
        app::tool::log::Logger::inst().log(log_level, tag, format, ##__VA_ARGS__);                 \
    } while (0)

#define LOG_DEBUG(tag, format, ...)                                                                \
    app::tool::log::Logger::inst().log(app::tool::log::LogLevel::DEBUG, tag, format, ##__VA_ARGS__)

#define LOG_INFO(tag, format, ...)                                                                 \
    app::tool::log::Logger::inst().log(app::tool::log::LogLevel::INFO, tag, format, ##__VA_ARGS__)

#define LOG_WARN(tag, format, ...)                                                                 \
    app::tool::log::Logger::inst().log(app::tool::log::LogLevel::WARN, tag, format, ##__VA_ARGS__)

#define LOG_ERROR(tag, format, ...)                                                                \
    app::tool::log::Logger::inst().log(app::tool::log::LogLevel::ERROR, tag, format, ##__VA_ARGS__)

} // namespace app::tool::log
