/**
 * @file log.cc
 * @brief 异步日志管理系统实现
 */

#include "log.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

namespace glasses {
namespace tool {
namespace logger {

// ============================================================================
// ANSI 颜色代码
// ============================================================================

#define ANSI_COLOR_RESET   "\033[0m"
#define ANSI_COLOR_BLUE    "\033[34m"  // DEBUG - 蓝色
#define ANSI_COLOR_WHITE   "\033[37m"  // INFO  - 白色
#define ANSI_COLOR_YELLOW  "\033[33m"  // WARN  - 黄色
#define ANSI_COLOR_RED     "\033[31m"  // ERROR - 红色

// ============================================================================
// Logger 实现
// ============================================================================

Logger::Logger()
    : running_(false)
    , initialized_(false)
    , buffer_(nullptr)
    , write_index_(0)
    , read_index_(0)
    , message_count_(0)
    , log_file_(nullptr)
    , current_file_size_(0) {
}

Logger::~Logger() {
    shutdown();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

bool Logger::initialize(const LogConfig& config) {
    if (initialized_.load()) {
        return true;  // 已经初始化
    }
    
    config_ = config;
    
    // 创建环形缓冲区
    buffer_ = std::make_unique<LogMessage[]>(config_.buffer_size);
    if (!buffer_) {
        fprintf(stderr, "[Logger] Failed to allocate buffer\n");
        return false;
    }
    
    // 打开日志文件
    if (config_.enable_file) {
        // 提取目录路径并创建目录
        std::string log_dir = config_.log_file_path;
        size_t last_slash = log_dir.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            log_dir = log_dir.substr(0, last_slash);
            
            // 检查目录是否存在，不存在则创建
            struct stat st;
            if (stat(log_dir.c_str(), &st) != 0) {
                // 目录不存在，创建目录
                if (mkdir(log_dir.c_str(), 0755) != 0) {
                    fprintf(stderr, "[Logger] Failed to create log directory: %s\n", 
                            log_dir.c_str());
                    return false;
                }
                fprintf(stdout, "[Logger] Created log directory: %s\n", log_dir.c_str());
            }
        }
        
        // 打开日志文件
        log_file_ = fopen(config_.log_file_path.c_str(), "w");
        if (!log_file_) {
            fprintf(stderr, "[Logger] Failed to open log file: %s\n", 
                    config_.log_file_path.c_str());
            return false;
        }
        
        // 写入文件头
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char time_buffer[64];
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
        
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
    running_ = true;
    worker_thread_ = std::thread(&Logger::workerThread, this);
    
    initialized_ = true;
    
    // 输出初始化成功消息
    log(LogLevel::INFO, "Logger", "Logger initialized successfully");
    log(LogLevel::INFO, "Logger", "  Console: %s", config_.enable_console ? "enabled" : "disabled");
    log(LogLevel::INFO, "Logger", "  File: %s", config_.enable_file ? "enabled" : "disabled");
    log(LogLevel::INFO, "Logger", "  Color: %s", config_.enable_color ? "enabled" : "disabled");
    log(LogLevel::INFO, "Logger", "  Timestamp: %s", config_.enable_timestamp ? "enabled" : "disabled");
    log(LogLevel::INFO, "Logger", "  Thread ID: %s", config_.enable_thread_id ? "enabled" : "disabled");
    log(LogLevel::INFO, "Logger", "  Buffer Size: %zu messages", config_.buffer_size);
    log(LogLevel::INFO, "Logger", "  Max File Size: %.2f MB", config_.max_file_size / (1024.0 * 1024.0));
    
    return true;
}

void Logger::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    
    log(LogLevel::INFO, "Logger", "Logger shutting down...");
    
    // 停止后台线程
    running_ = false;
    cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // 关闭日志文件
    if (log_file_) {
        fprintf(log_file_, "\n========================================\n");
        fprintf(log_file_, "  Logger Shutdown\n");
        fprintf(log_file_, "========================================\n");
        fclose(log_file_);
        log_file_ = nullptr;
    }
    
    initialized_ = false;
}

void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
    if (!initialized_.load() || level < config_.min_level) {
        return;
    }
    
    // 格式化日志消息
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log(level, tag, std::string(buffer));
}

void Logger::log(LogLevel level, const char* tag, const std::string& message) {
    if (!initialized_.load() || level < config_.min_level) {
        return;
    }
    
    // 检查缓冲区是否已满
    if (message_count_.load() >= config_.buffer_size) {
        // 缓冲区满，丢弃消息（可以考虑阻塞或扩展缓冲区）
        static std::atomic<int> drop_count(0);
        if (++drop_count % 100 == 1) {  // 每100条丢弃消息输出一次警告
            fprintf(stderr, "[Logger] Warning: Buffer full, dropping messages (total: %d)\n", 
                    drop_count.load());
        }
        return;
    }
    
    // 写入环形缓冲区
    size_t index = write_index_.fetch_add(1) % config_.buffer_size;
    LogMessage& msg = buffer_[index];
    
    msg.level = level;
    msg.timestamp = getCurrentTimestamp();
    msg.tag = tag ? tag : "";
    msg.message = message;
    msg.thread_id = std::this_thread::get_id();
    
    // 增加消息计数
    message_count_.fetch_add(1);
    
    // 通知后台线程
    cv_.notify_one();
}

void Logger::setMinLevel(LogLevel level) {
    config_.min_level = level;
}

void Logger::setConsoleEnabled(bool enabled) {
    config_.enable_console = enabled;
}

void Logger::setFileEnabled(bool enabled) {
    config_.enable_file = enabled;
}

void Logger::setColorEnabled(bool enabled) {
    config_.enable_color = enabled;
}

void Logger::setTimestampEnabled(bool enabled) {
    config_.enable_timestamp = enabled;
}

void Logger::setThreadIdEnabled(bool enabled) {
    config_.enable_thread_id = enabled;
}

void Logger::flush() {
    if (log_file_) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        fflush(log_file_);
    }
}

bool Logger::isRunning() const {
    return running_.load() && initialized_.load();
}

// ============================================================================
// 私有方法
// ============================================================================

void Logger::workerThread() {
    while (running_.load()) {
        // 等待消息或超时
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
            return message_count_.load() > 0 || !running_.load();
        });
        
        // 处理所有待处理的消息
        while (message_count_.load() > 0) {
            size_t index = read_index_.fetch_add(1) % config_.buffer_size;
            const LogMessage& msg = buffer_[index];
            
            // 写入控制台
            if (config_.enable_console) {
                writeToConsole(msg);
            }
            
            // 写入文件
            if (config_.enable_file && log_file_) {
                writeToFile(msg);
            }
            
            // 减少消息计数
            message_count_.fetch_sub(1);
        }
        
        // 定期刷新文件
        if (log_file_) {
            std::lock_guard<std::mutex> file_lock(file_mutex_);
            fflush(log_file_);
        }
    }
    
    // 退出前处理剩余消息
    while (message_count_.load() > 0) {
        size_t index = read_index_.fetch_add(1) % config_.buffer_size;
        const LogMessage& msg = buffer_[index];
        
        if (config_.enable_console) {
            writeToConsole(msg);
        }
        
        if (config_.enable_file && log_file_) {
            writeToFile(msg);
        }
        
        message_count_.fetch_sub(1);
    }
    
    // 最后刷新
    if (log_file_) {
        fflush(log_file_);
    }
}

void Logger::writeToConsole(const LogMessage& msg) {
    std::string formatted = formatMessage(msg, config_.enable_color);
    fprintf(stdout, "%s\n", formatted.c_str());
    fflush(stdout);
}

void Logger::writeToFile(const LogMessage& msg) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (!log_file_) {
        return;
    }
    
    // 检查文件轮转
    rotateFileIfNeeded();
    
    // 格式化消息（不带颜色）
    std::string formatted = formatMessage(msg, false);
    
    // 写入文件
    size_t written = fprintf(log_file_, "%s\n", formatted.c_str());
    
    // 更新文件大小
    current_file_size_.fetch_add(written);
}

std::string Logger::formatMessage(const LogMessage& msg, bool with_color) {
    char buffer[8192];
    
    // 格式化时间戳
    time_t seconds = msg.timestamp / 1000000;
    uint64_t microseconds = msg.timestamp % 1000000;
    struct tm* tm_info = localtime(&seconds);
    char time_buffer[64];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 获取日志级别
    const char* level_str = getLevelString(msg.level);
    
    // 获取线程ID（简短格式）
    std::hash<std::thread::id> hasher;
    size_t thread_hash = hasher(msg.thread_id) % 10000;
    
    // 格式化日志
    if (config_.enable_timestamp) {
        // 显示时间戳
        if (with_color) {
            const char* color = getLevelColor(msg.level);
            if (config_.enable_thread_id) {
                snprintf(buffer, sizeof(buffer), 
                        "[%s.%06llu] %s[%-5s] [%s]%s [T%04zu] %s",
                        time_buffer,
                        (unsigned long long)microseconds,
                        color,
                        level_str,
                        msg.tag.c_str(),
                        ANSI_COLOR_RESET,
                        thread_hash,
                        msg.message.c_str());
            } else {
                snprintf(buffer, sizeof(buffer), 
                        "[%s.%06llu] %s[%-5s] [%s]%s %s",
                        time_buffer,
                        (unsigned long long)microseconds,
                        color,
                        level_str,
                        msg.tag.c_str(),
                        ANSI_COLOR_RESET,
                        msg.message.c_str());
            }
        } else {
            if (config_.enable_thread_id) {
                snprintf(buffer, sizeof(buffer), 
                        "[%s.%06llu] [%-5s] [%s] [T%04zu] %s",
                        time_buffer,
                        (unsigned long long)microseconds,
                        level_str,
                        msg.tag.c_str(),
                        thread_hash,
                        msg.message.c_str());
            } else {
                snprintf(buffer, sizeof(buffer), 
                        "[%s.%06llu] [%-5s] [%s] %s",
                        time_buffer,
                        (unsigned long long)microseconds,
                        level_str,
                        msg.tag.c_str(),
                        msg.message.c_str());
            }
        }
    } else {
        // 不显示时间戳
        if (with_color) {
            const char* color = getLevelColor(msg.level);
            if (config_.enable_thread_id) {
                snprintf(buffer, sizeof(buffer), 
                        "%s[%-5s] [%s]%s [T%04zu] %s",
                        color,
                        level_str,
                        msg.tag.c_str(),
                        ANSI_COLOR_RESET,
                        thread_hash,
                        msg.message.c_str());
            } else {
                snprintf(buffer, sizeof(buffer), 
                        "%s[%-5s] [%s]%s %s",
                        color,
                        level_str,
                        msg.tag.c_str(),
                        ANSI_COLOR_RESET,
                        msg.message.c_str());
            }
        } else {
            if (config_.enable_thread_id) {
                snprintf(buffer, sizeof(buffer), 
                        "[%-5s] [%s] [T%04zu] %s",
                        level_str,
                        msg.tag.c_str(),
                        thread_hash,
                        msg.message.c_str());
            } else {
                snprintf(buffer, sizeof(buffer), 
                        "[%-5s] [%s] %s",
                        level_str,
                        msg.tag.c_str(),
                        msg.message.c_str());
            }
        }
    }
    
    return std::string(buffer);
}

void Logger::rotateFileIfNeeded() {
    // 检查当前文件大小
    if (current_file_size_.load() < config_.max_file_size) {
        return;
    }
    
    // 关闭当前文件
    if (log_file_) {
        fprintf(log_file_, "\n========================================\n");
        fprintf(log_file_, "  File Rotation (size: %.2f MB)\n", 
                current_file_size_.load() / (1024.0 * 1024.0));
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
    if (!log_file_) {
        fprintf(stderr, "[Logger] Failed to create new log file after rotation\n");
        return;
    }
    
    // 写入新文件头
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buffer[64];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    
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

const char* Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* Logger::getLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return ANSI_COLOR_BLUE;
        case LogLevel::INFO:  return ANSI_COLOR_WHITE;
        case LogLevel::WARN:  return ANSI_COLOR_YELLOW;
        case LogLevel::ERROR: return ANSI_COLOR_RED;
        default: return ANSI_COLOR_WHITE;
    }
}

uint64_t Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace logger
} // namespace tool
} // namespace glasses

