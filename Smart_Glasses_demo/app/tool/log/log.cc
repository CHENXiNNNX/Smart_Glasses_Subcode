/**
 * @file log.cc
 * @brief 异步日志管理系统实现
 */

#include "log.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

// ============================================================================
// 常量定义
// ============================================================================

#define LOG_DIR_PERMISSION 0755         // 日志目录权限
#define TIME_BUFFER_SIZE 64             // 时间缓冲区大小
#define LOG_BUFFER_SIZE 4096            // 日志缓冲区大小
#define DROP_WARNING_INTERVAL 100       // 每N条丢弃消息输出一次警告
#define WORKER_THREAD_TIMEOUT_MS 100    // 工作线程等待超时（毫秒）
#define FORMAT_BUFFER_SIZE 8192         // 格式化缓冲区大小
#define MICROSECONDS_PER_SECOND 1000000 // 每秒微秒数
#define THREAD_HASH_MODULO 10000        // 线程哈希模数

// ============================================================================
// ANSI 颜色代码
// ============================================================================

#define ANSI_COLOR_RESET "\033[0m"
#define ANSI_COLOR_BLUE "\033[34m"   // DEBUG - 蓝色
#define ANSI_COLOR_WHITE "\033[37m"  // INFO  - 白色
#define ANSI_COLOR_YELLOW "\033[33m" // WARN  - 黄色
#define ANSI_COLOR_RED "\033[31m"    // ERROR - 红色

namespace app
{
    namespace tool
    {
        namespace log
        {

            // ============================================================================
            // Logger 实现
            // ============================================================================

            Logger::Logger()
                : running_(false), initialized_(false), buffer_(nullptr), write_index_(0),
                  read_index_(0), message_count_(0), log_file_(nullptr), current_file_size_(0)
            {
            }

            Logger::~Logger()
            {
                shutdown();
            }

            Logger& Logger::getInstance()
            {
                static Logger instance;
                return instance;
            }

            bool Logger::initialize(const LogConfig& config)
            {
                if (initialized_.load())
                {
                    return true; // 已经初始化
                }

                config_ = config;

                // 创建环形缓冲区
                buffer_ = std::make_unique<LogMessage[]>(config_.buffer_size);
                if (!buffer_)
                {
                    fprintf(stderr, "[Logger] Failed to allocate buffer\n");
                    return false;
                }

                // 打开日志文件
                if (config_.enable_file)
                {
                    // 提取目录路径并创建目录
                    std::string log_dir    = config_.log_file_path;
                    size_t      last_slash = log_dir.find_last_of("/\\");
                    if (last_slash != std::string::npos)
                    {
                        log_dir = log_dir.substr(0, last_slash);

                        // 检查目录是否存在，不存在则创建
                        struct stat stat_info;
                        if (stat(log_dir.c_str(), &stat_info) != 0)
                        {
                            // 目录不存在，创建目录
                            if (mkdir(log_dir.c_str(), LOG_DIR_PERMISSION) != 0)
                            {
                                fprintf(stderr, "[Logger] Failed to create log directory: %s\n",
                                        log_dir.c_str());
                                return false;
                            }
                            fprintf(stdout, "[Logger] Created log directory: %s\n",
                                    log_dir.c_str());
                        }
                    }

                    // 打开日志文件
                    log_file_ = fopen(config_.log_file_path.c_str(), "w");
                    if (!log_file_)
                    {
                        fprintf(stderr, "[Logger] Failed to open log file: %s\n",
                                config_.log_file_path.c_str());
                        return false;
                    }

                    // 写入文件头
                    time_t     current_time = time(nullptr);
                    struct tm* time_info    = localtime(&current_time);
                    char       time_buffer[TIME_BUFFER_SIZE];
                    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

                    fprintf(log_file_, "========================================\n");
                    fprintf(log_file_, "  Smart Glasses Log File\n");
                    fprintf(log_file_, "  Start Time: %s\n", time_buffer);
                    fprintf(log_file_, "========================================\n\n");
                    fflush(log_file_);

                    // 更新文件大小
                    fseek(log_file_, 0, SEEK_END);
                    current_file_size_ = ftell(log_file_);
                }

                // 启动后台线程
                running_       = true;
                worker_thread_ = std::thread(&Logger::workerThread, this);

                initialized_ = true;

                // 输出初始化成功消息
                log(LogLevel::INFO, "Logger", "Logger initialized successfully");
                log(LogLevel::INFO, "Logger", "  Console: %s",
                    config_.enable_console ? "enabled" : "disabled");
                log(LogLevel::INFO, "Logger", "  File: %s",
                    config_.enable_file ? "enabled" : "disabled");
                log(LogLevel::INFO, "Logger", "  Color: %s",
                    config_.enable_color ? "enabled" : "disabled");
                log(LogLevel::INFO, "Logger", "  Timestamp: %s",
                    config_.enable_timestamp ? "enabled" : "disabled");
                log(LogLevel::INFO, "Logger", "  Thread ID: %s",
                    config_.enable_thread_id ? "enabled" : "disabled");
                log(LogLevel::INFO, "Logger", "  Buffer Size: %zu messages", config_.buffer_size);
                log(LogLevel::INFO, "Logger", "  Max File Size: %.2f MB",
                    config_.max_file_size / (double)BYTES_PER_MB);

                return true;
            }

            bool Logger::shutdown()
            {
                if (!initialized_.load())
                {
                    return true; // 已经关闭，返回成功
                }

                log(LogLevel::INFO, "Logger", "Logger shutting down...");

                // 停止后台线程
                running_ = false;
                cv_.notify_all();

                if (worker_thread_.joinable())
                {
                    worker_thread_.join();
                }

                // 关闭日志文件
                bool success = true;
                if (log_file_)
                {
                    fprintf(log_file_, "\n========================================\n");
                    fprintf(log_file_, "  Logger Shutdown\n");
                    fprintf(log_file_, "========================================\n");
                    if (fclose(log_file_) != 0)
                    {
                        fprintf(stderr, "[Logger] Failed to close log file\n");
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
                {
                    return;
                }

                // 格式化日志消息
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
                {
                    return;
                }

                // 检查缓冲区是否已满
                if (message_count_.load() >= config_.buffer_size)
                {
                    // 缓冲区满，丢弃消息（可以考虑阻塞或扩展缓冲区）
                    static std::atomic<int> s_drop_count(0);
                    if (++s_drop_count % DROP_WARNING_INTERVAL == 1)
                    { // 每N条丢弃消息输出一次警告
                        fprintf(stderr,
                                "[Logger] Warning: Buffer full, dropping messages (total: %d)\n",
                                s_drop_count.load());
                    }
                    return;
                }

                // 写入环形缓冲区
                size_t      buffer_index = write_index_.fetch_add(1) % config_.buffer_size;
                LogMessage& log_msg      = buffer_[buffer_index];

                log_msg.level     = level;
                log_msg.timestamp = getCurrentTimestamp();
                log_msg.tag       = tag ? tag : "";
                log_msg.message   = message;
                log_msg.thread_id = std::this_thread::get_id();

                // 增加消息计数
                message_count_.fetch_add(1);

                // 通知后台线程
                cv_.notify_one();
            }

            void Logger::setMinLevel(LogLevel level)
            {
                config_.min_level = level;
            }

            void Logger::setConsoleEnabled(bool enabled)
            {
                config_.enable_console = enabled;
            }

            void Logger::setFileEnabled(bool enabled)
            {
                config_.enable_file = enabled;
            }

            void Logger::setColorEnabled(bool enabled)
            {
                config_.enable_color = enabled;
            }

            void Logger::setTimestampEnabled(bool enabled)
            {
                config_.enable_timestamp = enabled;
            }

            void Logger::setThreadIdEnabled(bool enabled)
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

            bool Logger::isRunning() const
            {
                return running_.load() && initialized_.load();
            }

            // ============================================================================
            // 私有方法
            // ============================================================================

            void Logger::workerThread()
            {
                while (running_.load())
                {
                    // 等待消息或超时
                    std::unique_lock<std::mutex> mutex_lock(mutex_);
                    cv_.wait_for(mutex_lock, std::chrono::milliseconds(WORKER_THREAD_TIMEOUT_MS),
                                 [this]()
                                 { return message_count_.load() > 0 || !running_.load(); });

                    // 处理所有待处理的消息
                    while (message_count_.load() > 0)
                    {
                        size_t buffer_index       = read_index_.fetch_add(1) % config_.buffer_size;
                        const LogMessage& log_msg = buffer_[buffer_index];

                        // 写入控制台
                        if (config_.enable_console)
                        {
                            writeToConsole(log_msg);
                        }

                        // 写入文件
                        if (config_.enable_file && log_file_)
                        {
                            writeToFile(log_msg);
                        }

                        // 减少消息计数
                        message_count_.fetch_sub(1);
                    }

                    // 定期刷新文件
                    if (log_file_)
                    {
                        std::lock_guard<std::mutex> file_mutex_lock(file_mutex_);
                        fflush(log_file_);
                    }
                }

                // 退出前处理剩余消息
                while (message_count_.load() > 0)
                {
                    size_t            buffer_index = read_index_.fetch_add(1) % config_.buffer_size;
                    const LogMessage& log_msg      = buffer_[buffer_index];

                    if (config_.enable_console)
                    {
                        writeToConsole(log_msg);
                    }

                    if (config_.enable_file && log_file_)
                    {
                        writeToFile(log_msg);
                    }

                    message_count_.fetch_sub(1);
                }

                // 最后刷新
                if (log_file_)
                {
                    fflush(log_file_);
                }
            }

            void Logger::writeToConsole(const LogMessage& msg)
            {
                std::string formatted = formatMessage(msg, config_.enable_color);
                fprintf(stdout, "%s\n", formatted.c_str());
                fflush(stdout);
            }

            void Logger::writeToFile(const LogMessage& msg)
            {
                std::lock_guard<std::mutex> mutex_lock(file_mutex_);

                if (!log_file_)
                {
                    return;
                }

                // 检查文件轮转
                rotateFileIfNeeded();

                // 格式化消息（不带颜色）
                std::string formatted = formatMessage(msg, false);

                // 写入文件
                size_t bytes_written = fprintf(log_file_, "%s\n", formatted.c_str());

                // 更新文件大小
                current_file_size_.fetch_add(bytes_written);
            }

            std::string Logger::formatMessage(const LogMessage& msg, bool with_color)
            {
                char log_buffer[FORMAT_BUFFER_SIZE];

                // 格式化时间戳
                time_t     timestamp_seconds = msg.timestamp / MICROSECONDS_PER_SECOND;
                uint64_t   microseconds      = msg.timestamp % MICROSECONDS_PER_SECOND;
                struct tm* time_info         = localtime(&timestamp_seconds);
                char       time_buffer[TIME_BUFFER_SIZE];
                strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

                // 获取日志级别
                const char* level_string = getLevelString(msg.level);

                // 获取线程ID（简短格式）
                std::hash<std::thread::id> thread_hasher;
                size_t thread_hash = thread_hasher(msg.thread_id) % THREAD_HASH_MODULO;

                // 格式化日志
                if (config_.enable_timestamp)
                {
                    // 显示时间戳
                    if (with_color)
                    {
                        const char* color_code = getLevelColor(msg.level);
                        if (config_.enable_thread_id)
                        {
                            snprintf(log_buffer, sizeof(log_buffer),
                                     "[%s.%06llu] %s[%-5s] [%s]%s [T%04zu] %s", time_buffer,
                                     (unsigned long long)microseconds, color_code, level_string,
                                     msg.tag.c_str(), ANSI_COLOR_RESET, thread_hash,
                                     msg.message.c_str());
                        }
                        else
                        {
                            snprintf(log_buffer, sizeof(log_buffer),
                                     "[%s.%06llu] %s[%-5s] [%s]%s %s", time_buffer,
                                     (unsigned long long)microseconds, color_code, level_string,
                                     msg.tag.c_str(), ANSI_COLOR_RESET, msg.message.c_str());
                        }
                    }
                    else
                    {
                        if (config_.enable_thread_id)
                        {
                            snprintf(log_buffer, sizeof(log_buffer),
                                     "[%s.%06llu] [%-5s] [%s] [T%04zu] %s", time_buffer,
                                     (unsigned long long)microseconds, level_string,
                                     msg.tag.c_str(), thread_hash, msg.message.c_str());
                        }
                        else
                        {
                            snprintf(log_buffer, sizeof(log_buffer), "[%s.%06llu] [%-5s] [%s] %s",
                                     time_buffer, (unsigned long long)microseconds, level_string,
                                     msg.tag.c_str(), msg.message.c_str());
                        }
                    }
                }
                else
                {
                    // 不显示时间戳
                    if (with_color)
                    {
                        const char* color_code = getLevelColor(msg.level);
                        if (config_.enable_thread_id)
                        {
                            snprintf(log_buffer, sizeof(log_buffer), "%s[%-5s] [%s]%s [T%04zu] %s",
                                     color_code, level_string, msg.tag.c_str(), ANSI_COLOR_RESET,
                                     thread_hash, msg.message.c_str());
                        }
                        else
                        {
                            snprintf(log_buffer, sizeof(log_buffer), "%s[%-5s] [%s]%s %s",
                                     color_code, level_string, msg.tag.c_str(), ANSI_COLOR_RESET,
                                     msg.message.c_str());
                        }
                    }
                    else
                    {
                        if (config_.enable_thread_id)
                        {
                            snprintf(log_buffer, sizeof(log_buffer), "[%-5s] [%s] [T%04zu] %s",
                                     level_string, msg.tag.c_str(), thread_hash,
                                     msg.message.c_str());
                        }
                        else
                        {
                            snprintf(log_buffer, sizeof(log_buffer), "[%-5s] [%s] %s", level_string,
                                     msg.tag.c_str(), msg.message.c_str());
                        }
                    }
                }

                return std::string(log_buffer);
            }

            void Logger::rotateFileIfNeeded()
            {
                // 检查当前文件大小
                if (current_file_size_.load() < config_.max_file_size)
                {
                    return;
                }

                // 关闭当前文件
                if (log_file_)
                {
                    fprintf(log_file_, "\n========================================\n");
                    fprintf(log_file_, "  File Rotation (size: %.2f MB)\n",
                            current_file_size_.load() / (double)BYTES_PER_MB);
                    fprintf(log_file_, "========================================\n\n");
                    fclose(log_file_);
                    log_file_ = nullptr;
                }

                // 文件轮转：将当前文件重命名为 .old
                std::string old_file_path = config_.log_file_path + ".old";

                // 删除旧的备份文件（如果存在）
                remove(old_file_path.c_str());

                // 将当前文件重命名为备份文件
                rename(config_.log_file_path.c_str(), old_file_path.c_str());

                // 创建新文件
                log_file_ = fopen(config_.log_file_path.c_str(), "w");
                if (!log_file_)
                {
                    fprintf(stderr, "[Logger] Failed to create new log file after rotation\n");
                    return;
                }

                // 写入新文件头
                time_t     current_time = time(nullptr);
                struct tm* time_info    = localtime(&current_time);
                char       time_buffer[TIME_BUFFER_SIZE];
                strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

                fprintf(log_file_, "========================================\n");
                fprintf(log_file_, "  Smart Glasses Log File (Rotated)\n");
                fprintf(log_file_, "  Rotation Time: %s\n", time_buffer);
                fprintf(log_file_, "========================================\n\n");
                fflush(log_file_);

                // 重置文件大小计数
                fseek(log_file_, 0, SEEK_END);
                current_file_size_ = ftell(log_file_);

                fprintf(stdout, "[Logger] Log file rotated: %s -> %s\n",
                        config_.log_file_path.c_str(), old_file_path.c_str());
            }

            const char* Logger::getLevelString(LogLevel level)
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

            const char* Logger::getLevelColor(LogLevel level)
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

            uint64_t Logger::getCurrentTimestamp()
            {
                auto current_time  = std::chrono::system_clock::now();
                auto time_duration = current_time.time_since_epoch();
                return std::chrono::duration_cast<std::chrono::microseconds>(time_duration).count();
            }

        } // namespace log
    }     // namespace tool
} // namespace app
