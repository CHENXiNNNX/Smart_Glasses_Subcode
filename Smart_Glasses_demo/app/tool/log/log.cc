/*
 * log.cc - 异步日志管理
 */

#include "log.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_DIR_PERMISSION 0755
#define TIME_BUFFER_SIZE 64
#define LOG_BUFFER_SIZE 4096
#define DROP_WARNING_INTERVAL 100
#define WORKER_THREAD_TIMEOUT_MS 100
#define FORMAT_BUFFER_SIZE 8192
#define MICROSECONDS_PER_SECOND 1000000
#define THREAD_HASH_MODULO 10000

#define ANSI_COLOR_RESET "\033[0m"
#define ANSI_COLOR_BLUE "\033[34m"
#define ANSI_COLOR_WHITE "\033[37m"
#define ANSI_COLOR_YELLOW "\033[33m"
#define ANSI_COLOR_RED "\033[31m"

namespace app::tool::log
{

    Logger::Logger()
        : running_(false), initialized_(false), buffer_(nullptr), write_index_(0), read_index_(0),
          message_count_(0), log_file_(nullptr), current_file_size_(0)
    {
    }

    Logger::~Logger()
    {
        deinit();
    }

    Logger& Logger::inst()
    {
        static Logger instance;
        return instance;
    }

    bool Logger::init(const LogConfig& config)
    {
        if (initialized_.load())
            return true;

        config_ = config;

        buffer_ = std::make_unique<LogMessage[]>(config_.buffer_size);
        if (!buffer_)
        {
            fprintf(stderr, "[Logger] 分配缓冲区失败\n");
            return false;
        }

        if (config_.enable_file)
        {
            std::string log_dir    = config_.log_file_path;
            size_t      last_slash = log_dir.find_last_of("/\\");
            if (last_slash != std::string::npos)
            {
                log_dir = log_dir.substr(0, last_slash);

                struct stat stat_info;
                if (stat(log_dir.c_str(), &stat_info) != 0)
                {
                    if (mkdir(log_dir.c_str(), LOG_DIR_PERMISSION) != 0)
                    {
                        fprintf(stderr, "[Logger] 创建日志目录失败: %s\n", log_dir.c_str());
                        return false;
                    }
                }
            }

            log_file_ = fopen(config_.log_file_path.c_str(), "w");
            if (!log_file_)
            {
                fprintf(stderr, "[Logger] 打开日志文件失败: %s\n", config_.log_file_path.c_str());
                return false;
            }

            time_t     current_time = time(nullptr);
            struct tm* time_info    = localtime(&current_time);
            char       time_buffer[TIME_BUFFER_SIZE];
            strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

            fprintf(log_file_, "========================================\n");
            fprintf(log_file_, "  Smart Glasses Log File\n");
            fprintf(log_file_, "  Start Time: %s\n", time_buffer);
            fprintf(log_file_, "========================================\n\n");
            fflush(log_file_);

            fseek(log_file_, 0, SEEK_END);
            current_file_size_ = ftell(log_file_);
        }

        running_       = true;
        worker_thread_ = std::thread(&Logger::worker_thread, this);
        initialized_   = true;

        log(LogLevel::INFO, "Logger", "启动");

        return true;
    }

    bool Logger::deinit()
    {
        if (!initialized_.load())
            return true;

        log(LogLevel::INFO, "Logger", "关闭");

        running_ = false;
        cv_.notify_all();

        if (worker_thread_.joinable())
            worker_thread_.join();

        bool success = true;
        if (log_file_)
        {
            fprintf(log_file_, "\n========================================\n");
            fprintf(log_file_, "  Logger Shutdown\n");
            fprintf(log_file_, "========================================\n");
            if (fclose(log_file_) != 0)
            {
                fprintf(stderr, "[Logger] 关闭日志文件失败\n");
                success = false;
            }
            log_file_ = nullptr;
        }

        initialized_ = false;
        return success;
    }

    void Logger::log(LogLevel level, const char* tag, const char* format, ...)
    {
        if (!initialized_.load() || level < config_.min_level)
            return;

        char    log_buffer[LOG_BUFFER_SIZE];
        va_list va_args;
        va_start(va_args, format);
        vsnprintf(log_buffer, sizeof(log_buffer), format, va_args);
        va_end(va_args);

        log(level, tag, std::string(log_buffer));
    }

    void Logger::log(LogLevel level, const char* tag, const std::string& message)
    {
        if (!initialized_.load() || level < config_.min_level)
            return;

        if (message_count_.load() >= config_.buffer_size)
        {
            static std::atomic<int> s_drop_count(0);
            if (++s_drop_count % DROP_WARNING_INTERVAL == 1)
            {
                fprintf(stderr, "[Logger] 缓冲区满，丢弃消息 (累计: %d)\n", s_drop_count.load());
            }
            return;
        }

        size_t      buffer_index = write_index_.fetch_add(1) % config_.buffer_size;
        LogMessage& log_msg      = buffer_[buffer_index];

        log_msg.level     = level;
        log_msg.timestamp = get_current_timestamp();
        log_msg.tag       = tag ? tag : "";
        log_msg.message   = message;
        log_msg.thread_id = std::this_thread::get_id();

        message_count_.fetch_add(1);
        cv_.notify_one();
    }

    void Logger::set_min_level(LogLevel level)
    {
        config_.min_level = level;
    }

    void Logger::set_console_enabled(bool enabled)
    {
        config_.enable_console = enabled;
    }

    void Logger::set_file_enabled(bool enabled)
    {
        config_.enable_file = enabled;
    }

    void Logger::set_color_enabled(bool enabled)
    {
        config_.enable_color = enabled;
    }

    void Logger::set_timestamp_enabled(bool enabled)
    {
        config_.enable_timestamp = enabled;
    }

    void Logger::set_thread_id_enabled(bool enabled)
    {
        config_.enable_thread_id = enabled;
    }

    void Logger::flush()
    {
        if (log_file_)
        {
            std::lock_guard<std::mutex> mutex_lock(file_mutex_);
            fflush(log_file_);
        }
    }

    bool Logger::is_running() const
    {
        return running_.load() && initialized_.load();
    }

    void Logger::worker_thread()
    {
        while (running_.load())
        {
            std::unique_lock<std::mutex> mutex_lock(mutex_);
            cv_.wait_for(mutex_lock, std::chrono::milliseconds(WORKER_THREAD_TIMEOUT_MS),
                         [this]() { return message_count_.load() > 0 || !running_.load(); });

            while (message_count_.load() > 0)
            {
                size_t            buffer_index = read_index_.fetch_add(1) % config_.buffer_size;
                const LogMessage& log_msg      = buffer_[buffer_index];

                if (config_.enable_console)
                    write_to_console(log_msg);

                if (config_.enable_file && log_file_)
                    write_to_file(log_msg);

                message_count_.fetch_sub(1);
            }

            if (log_file_)
            {
                std::lock_guard<std::mutex> file_mutex_lock(file_mutex_);
                fflush(log_file_);
            }
        }

        while (message_count_.load() > 0)
        {
            size_t            buffer_index = read_index_.fetch_add(1) % config_.buffer_size;
            const LogMessage& log_msg      = buffer_[buffer_index];

            if (config_.enable_console)
                write_to_console(log_msg);

            if (config_.enable_file && log_file_)
                write_to_file(log_msg);

            message_count_.fetch_sub(1);
        }

        if (log_file_)
            fflush(log_file_);
    }

    void Logger::write_to_console(const LogMessage& msg)
    {
        std::string formatted = format_message(msg, config_.enable_color);
        fprintf(stdout, "%s\n", formatted.c_str());
        fflush(stdout);
    }

    void Logger::write_to_file(const LogMessage& msg)
    {
        std::lock_guard<std::mutex> mutex_lock(file_mutex_);

        if (!log_file_)
            return;

        rotate_file_if_needed();

        std::string formatted     = format_message(msg, false);
        size_t      bytes_written = fprintf(log_file_, "%s\n", formatted.c_str());
        current_file_size_.fetch_add(bytes_written);
    }

    std::string Logger::format_message(const LogMessage& msg, bool with_color)
    {
        char log_buffer[FORMAT_BUFFER_SIZE];

        time_t     timestamp_seconds = msg.timestamp / MICROSECONDS_PER_SECOND;
        uint64_t   microseconds      = msg.timestamp % MICROSECONDS_PER_SECOND;
        struct tm* time_info         = localtime(&timestamp_seconds);
        char       time_buffer[TIME_BUFFER_SIZE];
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

        const char* level_string = get_level_string(msg.level);

        std::hash<std::thread::id> thread_hasher;
        size_t                     thread_hash = thread_hasher(msg.thread_id) % THREAD_HASH_MODULO;

        if (config_.enable_timestamp)
        {
            if (with_color)
            {
                const char* color_code = get_level_color(msg.level);
                if (config_.enable_thread_id)
                    snprintf(log_buffer, sizeof(log_buffer),
                             "[%s.%06llu] %s[%-5s] [%s]%s [T%04u] %s", time_buffer,
                             (unsigned long long)microseconds, color_code, level_string,
                             msg.tag.c_str(), ANSI_COLOR_RESET, (unsigned)thread_hash,
                             msg.message.c_str());
                else
                    snprintf(log_buffer, sizeof(log_buffer), "[%s.%06llu] %s[%-5s] [%s]%s %s",
                             time_buffer, (unsigned long long)microseconds, color_code,
                             level_string, msg.tag.c_str(), ANSI_COLOR_RESET, msg.message.c_str());
            }
            else
            {
                if (config_.enable_thread_id)
                    snprintf(log_buffer, sizeof(log_buffer), "[%s.%06llu] [%-5s] [%s] [T%04u] %s",
                             time_buffer, (unsigned long long)microseconds, level_string,
                             msg.tag.c_str(), (unsigned)thread_hash, msg.message.c_str());
                else
                    snprintf(log_buffer, sizeof(log_buffer), "[%s.%06llu] [%-5s] [%s] %s",
                             time_buffer, (unsigned long long)microseconds, level_string,
                             msg.tag.c_str(), msg.message.c_str());
            }
        }
        else
        {
            if (with_color)
            {
                const char* color_code = get_level_color(msg.level);
                if (config_.enable_thread_id)
                    snprintf(log_buffer, sizeof(log_buffer), "%s[%-5s] [%s]%s [T%04u] %s",
                             color_code, level_string, msg.tag.c_str(), ANSI_COLOR_RESET,
                             (unsigned)thread_hash, msg.message.c_str());
                else
                    snprintf(log_buffer, sizeof(log_buffer), "%s[%-5s] [%s]%s %s", color_code,
                             level_string, msg.tag.c_str(), ANSI_COLOR_RESET, msg.message.c_str());
            }
            else
            {
                if (config_.enable_thread_id)
                    snprintf(log_buffer, sizeof(log_buffer), "[%-5s] [%s] [T%04u] %s", level_string,
                             msg.tag.c_str(), (unsigned)thread_hash, msg.message.c_str());
                else
                    snprintf(log_buffer, sizeof(log_buffer), "[%-5s] [%s] %s", level_string,
                             msg.tag.c_str(), msg.message.c_str());
            }
        }

        return std::string(log_buffer);
    }

    void Logger::rotate_file_if_needed()
    {
        if (current_file_size_.load() < config_.max_file_size)
            return;

        if (log_file_)
        {
            fprintf(log_file_, "\n========================================\n");
            fprintf(log_file_, "  File Rotation (size: %.2f MB)\n",
                    current_file_size_.load() / (double)BYTES_PER_MB);
            fprintf(log_file_, "========================================\n\n");
            fclose(log_file_);
            log_file_ = nullptr;
        }

        std::string old_file_path = config_.log_file_path + ".old";
        remove(old_file_path.c_str());
        rename(config_.log_file_path.c_str(), old_file_path.c_str());

        log_file_ = fopen(config_.log_file_path.c_str(), "w");
        if (!log_file_)
        {
            fprintf(stderr, "[Logger] 轮转后创建新日志文件失败\n");
            return;
        }

        time_t     current_time = time(nullptr);
        struct tm* time_info    = localtime(&current_time);
        char       time_buffer[TIME_BUFFER_SIZE];
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

        fprintf(log_file_, "========================================\n");
        fprintf(log_file_, "  Smart Glasses Log File (Rotated)\n");
        fprintf(log_file_, "  Rotation Time: %s\n", time_buffer);
        fprintf(log_file_, "========================================\n\n");
        fflush(log_file_);

        fseek(log_file_, 0, SEEK_END);
        current_file_size_ = ftell(log_file_);

        fprintf(stdout, "[Logger] 日志轮转: %s -> %s\n", config_.log_file_path.c_str(),
                old_file_path.c_str());
    }

    const char* Logger::get_level_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    const char* Logger::get_level_color(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return ANSI_COLOR_BLUE;
        case LogLevel::INFO:
            return ANSI_COLOR_WHITE;
        case LogLevel::WARN:
            return ANSI_COLOR_YELLOW;
        case LogLevel::ERROR:
            return ANSI_COLOR_RED;
        default:
            return ANSI_COLOR_WHITE;
        }
    }

    uint64_t Logger::get_current_timestamp()
    {
        auto current_time  = std::chrono::system_clock::now();
        auto time_duration = current_time.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(time_duration).count();
    }

} // namespace app::tool::log
