#ifndef LOG_H
#define LOG_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cstdarg>
#include <cstring>  // 用于 strcmp 函数

namespace app {
namespace tool {
namespace log {

// ============================================================================
// 日志级别
// ============================================================================

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    DEBUG = 0,  // 调试信息（蓝色）
    INFO  = 1,  // 普通信息（白色）
    WARN  = 2,  // 警告信息（黄色）
    ERROR = 3   // 错误信息（红色）
};

// ============================================================================
// 日志配置
// ============================================================================

/**
 * @brief 日志系统配置
 */
struct LogConfig {
    bool enable_console;        // 是否输出到控制台
    bool enable_file;           // 是否输出到文件
    bool enable_color;          // 是否启用彩色输出
    bool enable_timestamp;      // 是否显示时间戳
    bool enable_thread_id;      // 是否显示线程ID
    std::string log_file_path;  // 日志文件路径
    size_t max_file_size;       // 单个日志文件最大大小（字节）
    size_t buffer_size;         // 环形缓冲区大小（消息数量）
    LogLevel min_level;         // 最小日志级别
    
    LogConfig()
        : enable_console(true)
        , enable_file(true)
        , enable_color(true)
        , enable_timestamp(false)           // 默认不显示时间戳
        , enable_thread_id(false)          // 默认不显示线程id
        , log_file_path("./log/smart_glasses.log")  // 日志输出到 log 目录
        , max_file_size(5 * 1024 * 1024)  // 5MB
        , buffer_size(8192)                // 8K 消息
        , min_level(LogLevel::DEBUG) {
    }
};

// ============================================================================
// 日志消息结构
// ============================================================================

/**
 * @brief 日志消息
 */
struct LogMessage {
    LogLevel level;             // 日志级别
    uint64_t timestamp;         // 时间戳（微秒）
    std::string tag;            // 模块标签（如 "WebSocket"、"AIManager"）
    std::string message;        // 日志内容
    std::thread::id thread_id;  // 线程ID
    
    LogMessage() 
        : level(LogLevel::INFO)
        , timestamp(0)
        , thread_id(std::this_thread::get_id()) {
    }
};

// ============================================================================
// 日志系统类
// ============================================================================

/**
 * @brief 异步日志管理器（单例模式）
 * @details 线程安全的日志系统，使用环形缓冲区和后台线程异步写入
 */
class Logger {
public:
    /**
     * @brief 获取Logger单例
     */
    static Logger& getInstance();
    
    /**
     * @brief 初始化日志系统
     * @param config 日志配置
     * @return true-成功, false-失败
     */
    bool initialize(const LogConfig& config = LogConfig());
    
    /**
     * @brief 关闭日志系统
     */
    void shutdown();
    
    /**
     * @brief 写入日志（格式化）
     * @param level 日志级别
     * @param tag 模块标签
     * @param format 格式化字符串（printf风格）
     * @param ... 可变参数
     */
    void log(LogLevel level, const char* tag, const char* format, ...);
    
    /**
     * @brief 写入日志（字符串）
     * @param level 日志级别
     * @param tag 模块标签
     * @param message 日志消息
     */
    void log(LogLevel level, const char* tag, const std::string& message);
    
    /**
     * @brief 设置最小日志级别
     * @param level 最小级别
     */
    void setMinLevel(LogLevel level);
    
    /**
     * @brief 启用/禁用控制台输出
     */
    void setConsoleEnabled(bool enabled);
    
    /**
     * @brief 启用/禁用文件输出
     */
    void setFileEnabled(bool enabled);
    
    /**
     * @brief 启用/禁用彩色输出
     */
    void setColorEnabled(bool enabled);
    
    /**
     * @brief 启用/禁用时间戳显示
     */
    void setTimestampEnabled(bool enabled);
    
    /**
     * @brief 启用/禁用线程ID显示
     */
    void setThreadIdEnabled(bool enabled);
    
    /**
     * @brief 刷新所有缓冲区到文件
     */
    void flush();
    
    /**
     * @brief 检查日志系统是否正在运行
     */
    bool isRunning() const;

    // 禁止拷贝和赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    Logger();
    
    /**
     * @brief 析构函数
     */
    ~Logger();
    
    /**
     * @brief 后台日志写入线程
     */
    void workerThread();
    
    /**
     * @brief 写入消息到控制台
     */
    void writeToConsole(const LogMessage& msg);
    
    /**
     * @brief 写入消息到文件
     */
    void writeToFile(const LogMessage& msg);
    
    /**
     * @brief 格式化日志消息
     */
    std::string formatMessage(const LogMessage& msg, bool with_color);
    
    /**
     * @brief 检查并执行文件轮转
     */
    void rotateFileIfNeeded();
    
    /**
     * @brief 获取日志级别字符串
     */
    const char* getLevelString(LogLevel level);
    
    /**
     * @brief 获取日志级别颜色代码（ANSI）
     */
    const char* getLevelColor(LogLevel level);
    
    /**
     * @brief 获取当前时间戳（微秒）
     */
    static uint64_t getCurrentTimestamp();

private:
    // 配置
    LogConfig config_;
    
    // 运行状态
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    
    // 环形缓冲区（无锁队列）
    std::unique_ptr<LogMessage[]> buffer_;
    std::atomic<size_t> write_index_;
    std::atomic<size_t> read_index_;
    std::atomic<size_t> message_count_;
    
    // 后台线程
    std::thread worker_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    
    // 文件输出
    FILE* log_file_;
    std::atomic<size_t> current_file_size_;
    std::mutex file_mutex_;
};

// ============================================================================
// 便捷宏定义
// ============================================================================

/**
 * @brief 通用日志宏
 * @param level 日志级别（DEBUG/INFO/WARN/ERROR）
 * @param tag 模块标签（字符串）
 * @param format 格式化字符串
 * @param ... 可变参数
 * 
 * @example
 *   USER_LOG("INFO", "WebSocket", "Connected to %s", url.c_str());
 *   USER_LOG("ERROR", "AIManager", "Failed to initialize: %d", error_code);
 */
#define USER_LOG(level, tag, format, ...) \
    do { \
        app::tool::log::LogLevel log_level; \
        if (strcmp(level, "DEBUG") == 0) { \
            log_level = app::tool::log::LogLevel::DEBUG; \
        } else if (strcmp(level, "INFO") == 0) { \
            log_level = app::tool::log::LogLevel::INFO; \
        } else if (strcmp(level, "WARN") == 0) { \
            log_level = app::tool::log::LogLevel::WARN; \
        } else if (strcmp(level, "ERROR") == 0) { \
            log_level = app::tool::log::LogLevel::ERROR; \
        } else { \
            log_level = app::tool::log::LogLevel::INFO; \
        } \
        app::tool::log::Logger::getInstance().log(log_level, tag, format, ##__VA_ARGS__); \
    } while(0)

/**
 * @brief 快捷日志宏
 */
#define LOG_DEBUG(tag, format, ...) \
    app::tool::log::Logger::getInstance().log( \
        app::tool::log::LogLevel::DEBUG, tag, format, ##__VA_ARGS__)

#define LOG_INFO(tag, format, ...) \
    app::tool::log::Logger::getInstance().log( \
        app::tool::log::LogLevel::INFO, tag, format, ##__VA_ARGS__)

#define LOG_WARN(tag, format, ...) \
    app::tool::log::Logger::getInstance().log( \
        app::tool::log::LogLevel::WARN, tag, format, ##__VA_ARGS__)

#define LOG_ERROR(tag, format, ...) \
    app::tool::log::Logger::getInstance().log( \
        app::tool::log::LogLevel::ERROR, tag, format, ##__VA_ARGS__)

} // namespace log
} // namespace tool
} // namespace app

#endif // LOG_H

